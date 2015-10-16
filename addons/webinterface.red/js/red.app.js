/*
 *	  Copyright (C) 2005-2013 Team XBMC
 *	  http://www.xbmc.org
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

(function (window, $) {
	"use strict";

	var RED = window.RED || {};

	RED.configuration = {
		addonId : 'script.red.helper'
	};

	RED.tools = {
		'loadedScripts' : [],

		/**
		 * Preloads JavaScript files and triggers a callback after loading completed
		 * 
		 * @param array scripts The script URLs to load
		 * @param function callback Callback function to call after loading completed 
		 */
		'preloadJavaScript' : function (scripts, callback) {
			if (scripts.length) {
				var script = scripts.shift();
				if (RED.tools.loadedScripts.indexOf(script) == -1) {
					$.ajax({
						url : script,
						dataType : 'script',
						cache: false, // set to false for debuggin/development
						success: function(data, status) {
							if (status === 'success' || status === 'notmodified') {
								RED.tools.preloadJavaScript(scripts, callback);
							}
						}
					});
				} else {
					RED.tools.preloadJavaScript(scripts, callback);
				}
			} else {
				callback();
			}
		},

		/**
		 * Sends a addon related action to xbmc
		 *
		 * @param string	id			The id of the addon
		 * @param string	action		The name of the action
		 * @param object	params		additional params to send (optional)
		 * @param boolean	wait		Tells RPC to wait for a response
		 * @param mixed		callback	callback function or object with callback functions (optional)
		 * @return void
		 */
		'sendAddonAction': function(id, action, params, wait, callback) {
			var addonId = id || RED.configuration.addonId;
			if (addonId) {
				xbmc.rpc.request({
					'method': 'Addons.ExecuteAddon',
					'params': {
						'addonid': addonId,
						'params': $.extend(params, {'action': action}),
						'wait' : wait ? true : false
					},
					'success': function(data, status) {
						if (typeof(callback) == 'function') return callback(data, status);
						if (typeof(callback) == 'object' && callback.success) callback.success(data, status); 
					},
					'error': function(data, status) {
						if (typeof(callback) == 'function') return callback(data, status);
						if (typeof(callback) == 'object' && callback.error) callback.error(data, status);
					}
				});
			}
		},
		/**
		 * capitalizes the first letter of a string
		 * 
		 * @param string
		 * @return string
		 */
		'capitaliseFirstLetter' : function(string) {
			return string.charAt(0).toUpperCase() + string.slice(1);
		}
	};


	RED.app = function() {
		this.load();	
	};

	RED.app.prototype = {
		constructor: RED.app,

		'load': function() {
			var self = this
				, scripts = [
					"/js/vendor/xbmc/xbmc.core.js",
					"/js/vendor/xbmc/xbmc.rpc.js",
					"/js/vendor/xbmc/xbmc.player.js"
				];
		
			if (typeof(JSON) == 'undefined' || typeof(JSON.stringify) == 'undefined') {
				scripts.push("/js/vendor/json2.js");	
			}

			RED.tools.preloadJavaScript(scripts, function() {self.init(); } );
		},
		'init': function() {
			var self = this;

			// bind player info if on site
			var $player = $('[data-type="xbmc-player-info"]');
			if ($player.length) {
				self.getPlayer().bindGuiElements($player);
			}

			// check for now playing placeholder
			var $nowPlaying = $('[data-type="xbmc-player-item"]');
			if ($nowPlaying.length) {
				self.initNowPlaying($nowPlaying);
			}

			// check for GUI buttons to be forwarded to helper script
			var $actions = $('[data-type="xbmc-action"]');
			if ($actions.length) {
				this.initActions($actions);
			}

			// check for volume controls
			var $volumeControls = $('[data-type="xbmc-player-volume"]');
			if($volumeControls.length) {
				this.initVolumeControls($volumeControls);
			}

			// check for progress bar
			var $progress = $('[data-type="xbmc-player-progress"]');
			if($progress.length) {
				this.initProgressBar($progress);
			}

			// check for the machine settings page
			var $settings = $('[data-type="red-settings"]');
			if ($settings.length) {
				RED.tools.preloadJavaScript(["/js/red.settings.js"], function() {
					self.settings = new RED.settings($settings, {
						addonId: RED.configuration.addonId,
						methodWrite:'SaveSettings',
						fileName:'/vfs/special%3A%2F%2Fred%2Fwebdata%2FPlayerSetup.xml'
					});
				});
			}

		},
		'initNowPlaying': function($items) {
			var player = this.getPlayer()
				, self = this
				, itemData = player.getPlayingItem() || {};

			// bind to data updates
			$(player).bind('onPlayingItemUpdated.REDnowPlaying', function(e, status, data, player) {
				if (status == 'success') {
					xbmc.core.fillPlaceholders($items, data.result.item);
					xbmc.rpc.request({
						'method': 'XBMC.GetInfoLabels',
						'params': {
							'labels': [
								'Player.Art(thumb)'
							]
						},
						'success': function(thumbdata) {
							// special handling for posters to get a nice fade effect instead of a simple replacement
					    self.updatePoster( $(xbmc.core.getFormattedProperty('Player.Art(thumb)', thumbdata.result)) );
						},
					});
				}
			});

			// trigger initial update
			xbmc.core.fillPlaceholders($items, itemData);
			xbmc.rpc.request({
				'method': 'XBMC.GetInfoLabels',
				'params': {
					'labels': [
						'Player.Art(thumb)'
					]
				},
				'success': function(thumbdata) {
					// special handling for posters to get a nice fade effect instead of a simple replacement
			    self.updatePoster( $(xbmc.core.getFormattedProperty('Player.Art(thumb)', thumbdata.result)) );
				},
			});
		},

		'initActions': function($toggles) {
			var self = this;
			$toggles.bind('click.xbmc', function(e) {
				var $t = $(this)
					, confirmation = $t.data('confirm')
					, eventParams = $t.data('action');
				if (confirmation) {
					bootbox.confirm(confirmation, function(result) {
						if (result) {
							self.sendClickEvent(eventParams);
						}
					});
				} else {
					self.sendClickEvent(eventParams);
				}
				return e.preventDefault();
			});
		},

		'initProgressBar': function($progressBar) {
			var self = this
				, player = this.getPlayer()
				, $indicator = $progressBar.find('[role="progressbar"]');

			$(player).bind('onPlayerStatusUpdated.progress', function(e, status, result, playerData, obj) {
				if (status == 'success') {
					$indicator.width(Number(playerData.percentage).toFixed(2) + '%');
				} else {
					$indicator.width(0);
				}
			});
		},

		'initVolumeControls': function($controls) {
			RED.tools.preloadJavaScript(["/js/vendor/xbmc/xbmc.application.js"], function() {
				var system = new xbmc.application()
					, $mute = $controls.filter('[data-toggle="xbmc-volume-mute"]')
					, $slider = $controls.filter('[data-toggle="xbmc-volume"]');
				// volume slider
				if ($slider.length) {
					$slider.on('slideStop', function(e) {
						system.setVolume(e.value);
					});
					$(system).bind('onVolumeChange', function(e, volume) {
						$slider.slider('setValue', volume);
					});
				}
				// mute button
				if ($mute.length) {
					$mute.click(function(e) {
						// make the slider indicate the muted state by setting it to 0
						if (system.isMuted()) {
							$slider.slider('setValue', system.getVolume());
							system.setMute(false);
						} else {
							$slider.slider('setValue', 0);
							system.setMute(true);
						}
						return e.preventDefault();
					});
				}
			});
		},

		'updatePoster': function($newPoster) {
			var $poster = $('.track-poster');
			if ($poster.length) {
				var $old = $poster.children();
				if ($newPoster.length) {
					$old.addClass('old');
					$newPoster.hide()
						.prependTo($poster)
						.fadeIn(function() {
							$old.remove();
						});
				} else if ($old.length) {
					$old.fadeOut(function() {
						$old.remove();
					});
				}
			}
		},

		'sendClickEvent': function(params) {
			xbmc.rpc.request({
					'method': 'RED.SendClick',
					'params': {'click' : params}

			});
		},

		'getPlayer': function() {
			if (!this.player) {
				this.player = new xbmc.player();
			}
			return this.player;
		}
	};

	window.RED = RED;

	var app = new RED.app();

}(window, jQuery));