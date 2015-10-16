/*
 *	  Copyright (C) 2014 Franz Koch (da-anda@kodi.tv)
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


	RED.settings = function(element, options) {
		this.init(element, options);
	};

	RED.settings.defaults = {
		methodWrite:'SaveSettings',
		fileName:'/vfs/special%3A%2F%2Fred%2Fwebdata%2FPlayerSetup.xml',
		classes: {
			editing: 'editing',
			editable: 'editable'
		},
		// this is just a fallback solution until this information
		// is part of the XML itself. Any category or setting
		// can be marked as editable by setting the attribute 'readonly="false"'
		// in their tag
		makeEditable: {
			// enables editing of an entire settings category
			categories: ['player'],
			// enables editing of specific settings from categories
			settings: []
		},
		clientConfigurationMapping: {
			'Network.IPAddress'			: 'network_ip_address',
			'Network.GatewayAddress'	: 'network_default_gateway',
			'Network.DNS1Address'		: 'network_dns_server_1',
			'Network.DNS2Address'		: 'network_dns_server_2',
			'Network.SubnetMask'		: 'network_subnet_mask',
			'Network.DHCPAddress'		: 'network_dhcp',
			'RED.PlayerStatus'      : 'player_status'
		}
	};

	RED.settings.prototype = {
		constructor: RED.settings,

		'init': function(element, options) {
			var self = this;
			this.$e = $(element);
			this.fire = this.$e.length ? this.$e.add(this) : $(this);
			this.options = $.extend(true, RED.settings.defaults, options, this.$e.data());
			this.$settings = $();
			this.overrideSettings = {};
			this.$uploadField = this.$e.find('input[data-type="settingsUpload"]').val('');
			this.$uploadPreview = this.$e.find(this.$uploadField.data('preview')).hide();
			this.canUpload = false;
			this.$categories = this.$e.find('[data-type="settingsCategory"]').hide();

			// Check for the various File API support.
			if (window.File && window.FileReader && window.FileList) {
				this.canUpload = true;
			} else {
				this.$uploadField.parent().replaceWith('<div class="alert alert-info">We\'re sorry, but your browser does allow to read settings from local files. So you can only change the settings via the options below.</div>');
			}

			// bind FileReader to upload field so that we can instantly read the selected file and apply it
			if (this.canUpload && this.$uploadField.length) {
				this.$uploadField.bind('change', function(e) {
					e.stopPropagation();
					e.preventDefault();

					if (e.target.files.length == 1) {
						var file = e.target.files[0];

						if (file.type == 'text/xml') {
							self.loadFile(file, function(status, data) {
								if (status == 'success') {
									self.updateFileInfo(file);
									self.$settings = $('settings', $.parseXML(data));
									self.fillSettings(self.$settings);
								} else {
									bootbox.alert('File could not be read. Please try again.');
								}
							});
						} else {
							bootbox.alert('The selected file does not seem to be a vailid configuration file. Please select a valid xml configuration file.');
						}
					}
				});
			}

			// catch form submission and forward to addon
			this.$e.submit(function(e) {
				e.preventDefault();
				if (!self.$settings.length) return;

				var data = self.$e.serializeObject();
				if (data) {
					for (var setting in data) {
						self.$settings.find(setting).empty().text(data[setting]);
					}
				}
				self.saveSettings(self.$settings, function(e, status) {
					if (status == 'success') {
						self.fillSettings(self.$settings);
						bootbox.alert('Settings saved successfully');
					} else {
						bootbox.alert('Settings could not be saved');
					}
				});
				
			});

			// add listeners to edit buttons etc
			this.$e.find('[data-type="actions"]')
				.hide()
				.click(function(e) {
					if (e.target.tagName == 'A') {
						self.onAction($(e.target).data('toggle'), $(e.target));
						return e.preventDefault();
					}
				});

			// load default settings
			this.loadSettings(
				function(data, status) {
					if (status == 'success' && data) {
						self.$settings = $('settings', data);
						// if we have valid basic config
						// grab client settings and store them as "realtime" override values
						self.getClientConfiguration(function(data, status) {
							if (status == 'success' && data && data.result) {
								self.overrideSettings = self.mapSettings(data.result, self.options.clientConfigurationMapping);
								self.fillSettings(self.$settings);
							} else {
								bootbox.alert('Current settings could not be read from backend');
							}
						});
					} else {
						bootbox.alert('Current settings could not be read from backend');
					}
				}
			);

		},

		/**
		 * Fills the document with given settings
		 *
		 * @param jQuery	$settings	The settings as dom XML object
		 * @return void
		 */
		'fillSettings': function($settings) {
			var self = this;
			this.$categories.each(function() {
				var categoryName = $(this).data('category');
				self.fillCategory(categoryName, $settings.children(categoryName));
			});
		},

		/**
		 * Fills the document with the settings from a certain category
		 *
		 * @parsm string	category	The name of the category
		 * @param jQuery	$settings	The settings as dom XML object
		 * @return void
		 */
		'fillCategory': function(category, $settings) {
			var self = this
				, $category = this.$categories.filter('[data-category="' + category + '"]')
					.removeClass(this.options.classes.editing)
				, $container = $category.find('[data-type="container"]')
				, $template = $container.find('[data-type="template"]').hide()
				, $actionContainer = $category.find('[data-type="actions"]').hide();

			if (this.isCategoryEditable($settings) || this.hasCategoryEditableSettings($settings)) {
				$category.addClass(self.options.classes.editable);
				// show/hide action bar
				if ($actionContainer.length) {
					$actionContainer.show()
						.find('[data-toggle]').hide();
					$actionContainer.find('[data-toggle="edit"]').show();
				}
			}

			// fill container with settings
			if ($container.length && $settings.length) {
				$container.children().not($template).remove();
				$settings.children().each(function() {
					var $node = $template.clone()
								.show()
								.data('type', null)
								.attr('id', this.tagName)
								.attr('data-type', null)
								.appendTo($container)
						, properties = {
							'name'	: self.settingToLabel(this.tagName),
							'value'	: this.textContent || this.innerHTML
						};
						// apply existing override settings
						if (self.overrideSettings[this.tagName] != undefined) {
							properties.value = self.overrideSettings[this.tagName];
						}
					xbmc.core.fillPlaceholders($node, properties);
				});
				$category.show();
			}
		},

		/**
		 * Makes allowed settings of the given category editable
		 * 
		 * @param string	category	The id of the category
		 * @return void 
		 */
		'makeEditable': function(category) {
			var self = this
				, $settings = this.$settings.children(category)
				, $category = this.$categories.filter('[data-category="' + category + '"]')
				, entireCategoryEditable = this.isCategoryEditable($settings);

			$settings.children().each(function() {
				if (entireCategoryEditable || self.isSettingEditable(this)) {
					$category.addClass(self.options.classes.editing);
					var $row = $category.find('#' + this.tagName);
					$row.find('[data-property="value"]')
						.empty()
						.html('<input type="text" name="' + this.tagName + '" class="form-control" value="' + (this.textContent || this.innerHTML) + '" />');
				}
			});
		},

		/**
		 * Processes a button action
		 * 
		 * @param string	action		The action name
		 * @param jQuery	$trigger	The triggering object
		 * @return void 
		 */
		'onAction': function(action, $trigger) {
			var $category = $trigger.closest('[data-type="settingsCategory"]')
				, category = $category.data('category');
			switch(action) {
				case 'edit':
					$trigger.hide().siblings().show();
					this.makeEditable(category);
					break;
				case 'cancel':
					$trigger.hide().siblings().show();
					this.fillCategory(category, this.$settings.children(category));
					break;
			}
		},

		/**
		 * Checks if a category is allowed to be edited
		 * 
		 * @param jQuery $category
		 * @return boolean 
		 */
		'isCategoryEditable' : function($category) {
			// check for permission in configuration
			if (this.options.makeEditable.categories.indexOf($category.prop('tagName')) > -1) {
				return true;
			}
			// if not found in config, check XML tag
			var self = this
				, readonly = $category.attr('readonly') || true;
			if (readonly === false || readonly == '0' || readonly == 'false') {
				return true;
			}
			return false;
		},

		/**
		 * Checks if a category is has editable settings
		 * 
		 * @param jQuery $category
		 * @return boolean 
		 */
		'hasCategoryEditableSettings': function($category) {
			var self = this
				, isEditable = false;
			$category.children().each(function() {
				if (self.isSettingEditable(this)) {
					isEditable = true;
				}
			});
			return isEditable;
		},

		/**
		 * Is setting editable
		 * 
		 * @param object $setting
		 * @return boolean 
		 */
		'isSettingEditable' : function(setting) {
			// check for permission in configuration
			if (this.options.makeEditable.settings.indexOf(setting.tagName) > -1) {
				return true;
			}
			// if not found in config, check XML tag
			var readonly = setting.getAttribute('readonly') || true;
			if (readonly === false || readonly == '0' || readonly == 'false') {
				return true;
			}
			return false;
		},

		/**
		 * Loads the settings from a local file path
		 * 
		 * @param File File object to read
		 * @param function		callback	callback function
		 * @return file content
		 */
		'loadFile': function(file, callback) {
			var reader = new FileReader();
			reader.onload = function(e) {
				if (typeof(callback) == 'function') {
					callback('success', e.target.result);
				}
			};
			reader.onerror = function(e) {
				if (typeof(callback) == 'function') {
					callback('error', e);
				}
			};
			reader.readAsText(file);
		},

		/**
		 * Reads the settings
		 *
		 * @param function		callback	callback function
		 * @return void
		 */
		'loadSettings': function(callback) {
			var url = this.options.fileName;
			$.ajax({
				'url': url,
				'dataType': 'xml',
				'cache': false,
				'success': function(settings, status) {
					if (status == 'success' && settings) {
						if (typeof(callback) == 'function') {
							callback(settings, 'success');
						}
					}
				},
				'error': function(status) {
					if (typeof(callback) == 'function') {
						callback(status, 'error');
					}
				}
			});
		},

		/**
		 * Reads the settings
		 *
		 * @param object	settings	The settings to write/save
		 * @param mixed		callback	callback function or object with callback functions (optional)
		 * @return void
		 */
		'saveSettings': function($settings, callback) {
			var $temp = $('<div>').append($settings.clone());
			xbmc.rpc.request({
					'method': 'RED.SaveXML',
					'params': {'xml' : $temp.html()}

			});
			$temp.remove();
		},

		/**
		 * Grabs the network configuration from XBMC
		 * 
		 * @param mixed		callback	callback function or object with callback functions (optional)
		 * @return void
		 */
		'getClientConfiguration' : function(callback) {
			xbmc.rpc.request({
				'method': 'XBMC.GetInfoLabels',
				'params': {
					'labels': [
						'Network.IPAddress',
						'Network.MacAddress',
						'Network.SubnetMask',
						'Network.GatewayAddress',
						'Network.DNS1Address',
						'Network.DNS2Address',
						'Network.DHCPAddress',
						'RED.PlayerStatus'
					]
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
		},

		/**
		 * Maps settings from one namespace to another
		 * 
		 * @param object values
		 * @param object mappingConfiguration
		 * @return object 
		 */
		'mapSettings': function(values, mappingConfiguration) {
			var result = {};
			for (var key in values) {
				if (mappingConfiguration[key]) {
					result[ mappingConfiguration[key] ] = values[key];
				} else {
					result[key] = values[key];
				}
			}
			return result;
		},

		/**
		 * Updates the info area that indicate the currently selected file
		 * 
		 * @param File file	The file object
		 * @return void
		 */
		'updateFileInfo' : function(file) {
			this.$uploadPreview.show();
			var properties = {
				'name' : file.name,
				'size' : Number(file.size / 1024).toFixed(2) + ' kB',
				'date' : file.lastModifiedDate.toLocaleDateString()
			};
			xbmc.core.fillPlaceholders(this.$uploadPreview, properties);
		},

		/**
		 * Converts a setting name to a readable string
		 * 
		 * @param string setting
		 * @return string
		 */
		'settingToLabel' : function(setting) {
			var parts = setting.split('_')
				, label = '';
			parts.shift();
			for (var id in parts) {
				label += RED.tools.capitaliseFirstLetter(parts[id]) + ' ';
			}
			return label;
		}
	};

	window.RED = RED;

}(window, jQuery));


/**
 * This little method converts form values to JSON.
 * Copied from http://stackoverflow.com/questions/1184624/convert-form-data-to-js-object-with-jquery#answer-8407771
 */
(function($){
	$.fn.serializeObject = function(){

		var self = this,
			json = {},
			push_counters = {},
			patterns = {
				"validate": /^[a-zA-Z][a-zA-Z0-9_]*(?:\[(?:\d*|[a-zA-Z0-9_]+)\])*$/,
				"key":	  /[a-zA-Z0-9_]+|(?=\[\])/g,
				"push":	 /^$/,
				"fixed":	/^\d+$/,
				"named":	/^[a-zA-Z0-9_]+$/
			};


		this.build = function(base, key, value){
			base[key] = value;
			return base;
		};

		this.push_counter = function(key){
			if(push_counters[key] === undefined){
				push_counters[key] = 0;
			}
			return push_counters[key]++;
		};

		$.each($(this).serializeArray(), function(){

			// skip invalid keys
			if(!patterns.validate.test(this.name)){
				return;
			}

			var k,
				keys = this.name.match(patterns.key),
				merge = this.value,
				reverse_key = this.name;

			while((k = keys.pop()) !== undefined){

				// adjust reverse_key
				reverse_key = reverse_key.replace(new RegExp("\\[" + k + "\\]$"), '');

				// push
				if(k.match(patterns.push)){
					merge = self.build([], self.push_counter(reverse_key), merge);
				}

				// fixed
				else if(k.match(patterns.fixed)){
					merge = self.build([], k, merge);
				}

				// named
				else if(k.match(patterns.named)){
					merge = self.build({}, k, merge);
				}
			}

			json = $.extend(true, json, merge);
		});

		return json;
	};
})(jQuery);