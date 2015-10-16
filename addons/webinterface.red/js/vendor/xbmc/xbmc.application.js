/*
 *      Copyright (C) 2005-2014 Team XBMC
 *      http://www.xbmc.org
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

(function (window) {

	var xbmc = window.xbmc || {};

	xbmc.application = function(options) {
		this.init(options);
	};

	xbmc.application.prototype = {
		constructor: xbmc.application,

		/**
		 * initializes the application object
		 *
		 * @param object		Optional overriding default options
		 * @return void
		 */
		'init': function(options) {
			this.fire = $(this);
			this.options = $.extend(true, {}, xbmc.defaults.application, options);
			this.data = {};
			this.timers = {};

			// callbacks
			var self = this;
			$.each(['onBeforeInit', 'onInit', 'onUpdate', 'onVolumeChange', 'onMute', 'onUnmute'], function(i, name) {
				// configuration
				if ($.isFunction(self.options[name])) { 
					$(self).bind(name, self.options[name]); 
				}
			});

			// onBeforeInit
			var e = $.Event("onBeforeInit");
			this.fire.trigger(e, [this]);

			// create timers to keep the gui etc updated
			// @todo	implement web sockets and get rid of the timeouts
			//			also move the update logic to xbmc.rpc and only bind listeners. xbmc.rpc should deal with sockets/timeouts
			$(this).bind('onUpdate.internal', function(e, status, result, obj) {
				// after update is finished queue next update call
				if (self.timers.data) clearTimeout(self.timers.data);
				var delay = status == 'success' ? 1000 : 5000;
				self.timers.data = setTimeout( function() { self._updateData(); self.timers.data = null; }, delay );
			});

			this._updateData();

			// onInit
			var e = $.Event("onInit");
			this.fire.trigger(e, [this]);
		},

		/**
		 * Get the assigned player
		 *
		 * @return xbmc.player
		 */
		'getVolume': function() {
			return this.data.volume;
		},

		/**
		 * Get sets the volume
		 *
		 * @param integer volume
		 * @return void
		 */
		'setVolume': function(volume) {
			xbmc.rpc.request({
				'method': 'Application.SetVolume',
				'params': {
					'volume': volume
				}
			});
			if (this.isMuted()) {
				this.setMute(false);
			}
		},

		/**
		 * Mutes the application
		 *
		 * @param integer volume
		 * @return void
		 */
		'setMute': function(mute) {
			xbmc.rpc.request({
				'method': 'Application.SetMute',
				'params': {
					'mute': mute ? true : false
				}
			});
		},

		/**
		 * toogles mute
		 *
		 * @return void
		 */
		'toggleMute': function() {
			this.setMute( !this.isMuted() );
		},

		/**
		 * returns the mute state
		 *
		 * @return boolean
		 */
		'isMuted': function() {
			return this.data.muted ? true : false;
		},

		/**
		 * Updates the status of the given players. If no player ID is given, all active will be updated
		 *
		 * @param object	callbacks	Optional object with callback function on either success or error
		 * @return void
		 */
		'_updateData': function(callbacks) {
			xbmc.rpc.request({
				'context': this,
				 'method': 'Application.GetProperties',
				'params' : {
					'properties': ['volume', 'muted', 'name', 'version']
				},
				'timeout': 3000,
				'success': function(data) {
					if (data && data.result) {
						if (this.data.volume != data.result.volume) {
							var e = $.Event("onVolumeChange");
							this.fire.trigger(e, [data.result.volume, this]);
						}
						if (this.data.muted != data.result.muted) {
							var e = $.Event(data.result.muted ? 'onMuted' : 'onUnmuted');
							this.fire.trigger(e, [this]);
						}
						this.data = data.result;
					} else {
						this.data = {};
					}

					if (callbacks && typeof(callbacks.success) == 'function') callbacks.success(data, this);

					// fire event
					var e = $.Event("onUpdate");
					this.fire.trigger(e, ['success', data, this]);
				},
				'error': function(data, error) {
					xbmc.core.displayCommunicationError();
					if (callbacks && typeof(callbacks.error) == 'function') callbacks.error(data, this);

					// fire event
					var e = $.Event("onUpdate");
					this.fire.trigger(e, ['error', data, this]);
				}
			});
		}
	};

	if (!xbmc.defaults) xbmc.defaults = {};
	xbmc.defaults.application = {};

	window.xbmc = xbmc;

}(window));