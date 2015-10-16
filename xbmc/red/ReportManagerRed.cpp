/*
 *  Copyright (C) 2014 Team RED
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "ReportManagerRed.h"
#include "DBManagerRed.h"
#include "UtilitiesRed.h"

#include "FileItem.h"
#include "filesystem/File.h"
#include "filesystem/Directory.h"
#include "filesystem/CurlFile.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/XBMCTinyXML.h"
#include "utils/XMLUtils.h"

CReportManagerRed::CReportManagerRed(const std::string &home, PlayerInfo *player_info)
 : CThread("CReportManagerRed")
 , m_strHome(home)
 , m_NextReportTime(CDateTime::GetCurrentDateTime())
 , m_NextReportInterval(0, 1, 0, 0)
 , m_PlayerInfo(player_info)
 , m_ReportManagerCallBackFn(NULL)
 , m_ReportManagerCallBackCtx(NULL)
{
}

CReportManagerRed::~CReportManagerRed()
{
  m_ReportManagerCallBackFn = NULL;
  StopThread();
}

void CReportManagerRed::SendReport()
{
  m_SendReport.Set();
}

void CReportManagerRed::SetReportInterval(const CDateTimeSpan &interval)
{
  m_NextReportInterval = interval;
}

void CReportManagerRed::RegisterReportManagerCallBack(const void *ctx, ReportManagerCallBackFn fn)
{
  m_ReportManagerCallBackFn = fn;
  m_ReportManagerCallBackCtx = ctx;
}

void CReportManagerRed::Process()
{
  CLog::Log(LOGDEBUG, "**RED** - CReportManagerRed::Process Started");

  while (!m_bStop)
  {
    CDateTime time = CDateTime::GetCurrentDateTime();
    if (m_SendReport.WaitMSec(250) || time >= m_NextReportTime)
    {
      m_NextReportTime  = time;
      m_NextReportTime += m_NextReportInterval;
      SendDailyAssetReport();

      if (m_ReportManagerCallBackFn)
        (*m_ReportManagerCallBackFn)(m_ReportManagerCallBackCtx, true);
    }
  }

  CLog::Log(LOGDEBUG, "**RED** - CReportManagerRed::Process Stopped");
}

void CReportManagerRed::SendDailyAssetReport()
{
  /*
  new xml report format
  
  <xml>
  <date>2014-02-25</date>
  <playerId>1</playerId>
  <assets>
  <asset id="1039">2014-03-13 15:08:00</asset>
  <asset id="1040">2014-03-13 15:12:00</asset>
  <asset id="1041">2014-03-13 15:16:00</asset>
  </assets>
  </xml>
  */
  
  CDateTime date=CDateTime::GetCurrentDateTime();
  std::string playerId;
  CDBManagerRed database;
  database.Open();
  std::vector<RedMediaAsset> assets;
  database.GetAllPlayedAssets(assets);
  playerId = database.GetPlayerID();
  database.Close();
    
  CXBMCTinyXML xmlDoc;
  TiXmlElement xmlRootElement("xml");
  TiXmlNode *pRoot = xmlDoc.InsertEndChild(xmlRootElement);
  TiXmlElement pNodeD("date");
  TiXmlText value(date.GetAsDBDate());
  pNodeD.InsertEndChild(value);
  pRoot->InsertEndChild(pNodeD);
  TiXmlElement pNodeI("playerId");
  TiXmlText valueI(playerId);
  pNodeI.InsertEndChild(valueI);
  pRoot->InsertEndChild(pNodeI);
  
  TiXmlElement pNodeA("assets");
  
  for (std::vector<RedMediaAsset>::iterator assetit = assets.begin(); assetit != assets.end(); ++assetit)
  {
    TiXmlElement pNodeAsset("asset");
    pNodeAsset.SetAttribute("id", assetit->id.c_str());
    TiXmlText value(StringUtils::Format("%s", assetit->timePlayed.c_str()));
    pNodeAsset.InsertEndChild(value);
    pNodeA.InsertEndChild(pNodeAsset);
    CLog::Log(LOGDEBUG, "**RED** - asset id - %s from media group - %s played on - %s",
                                              assetit->id.c_str(),
                                              assetit->mediagroup_id.c_str(),
                                              assetit->timePlayed.c_str()
             );
  }
  pRoot->InsertEndChild(pNodeA);
  
  TiXmlPrinter printer;
  printer.SetLineBreak("\r\n");
  printer.SetIndent("  ");
  xmlDoc.Accept(&printer);
  std::string  strXml = printer.CStr();
  std::replace_if( strXml.begin(), strXml.end(), ::isspace, '+');

  std::string xmlEncoded = Encode(strXml);
  
  if (SendAssetReport(xmlEncoded))
  {
    database.Open();
    database.DeleteRecordsByUUID(assets);
    database.Close();
  }
}

bool CReportManagerRed::SendAssetReport(std::string xmlEncoded)
{
  // use shorter debug line, we know it works
  CLog::Log(LOGDEBUG, "**RED** - CReportManagerRed::SendAssetReport()"); 
  std::string function = "function=filePlayed&xml=" + xmlEncoded;
  std::string url = FormatUrl(*m_PlayerInfo, function);
  
  XFILE::CCurlFile http;
  std::string strXML;
  http.Post(url, "", strXML);
  
  TiXmlDocument xml;
  xml.Parse(strXML.c_str());
  
  TiXmlElement* rootXmlNode = xml.RootElement();
  if (rootXmlNode)
  {
    TiXmlElement* responseNode = rootXmlNode->FirstChildElement("response");
    if (responseNode)
    {
      std::string result; // 'Operation Successful'
      XMLUtils::GetString(responseNode, "result", result);
      CLog::Log(LOGDEBUG, "**RED** - CReportManagerRed::SendAssetReport() - Response '%s'" , result.c_str());
      if (result.find("Operation Successful") != std::string::npos)
        return true;
    }
  }
  
  return false;
}
