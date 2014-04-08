/*
 * Copyright (c) 2013, Ford Motor Company All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: ·
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. · Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. · Neither the name of the Ford Motor Company nor the
 * names of its contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * Reference implementation of BasicCommunication component. Interface to get or
 * set some essential information from OS. BasicCommunication responsible for
 * handling the basic commands of non-graphical part such as the registration of
 * mobile apps, geting the list of devices and applications and data transfer.
 */

FFW.BasicCommunication = FFW.RPCObserver
    .create( {

        /**
         * access to basic RPC functionality
         */
        client: FFW.RPCClient.create( {
            componentName: "BasicCommunication"
        }),


        //OnPutFile


        onPutFileSubscribeRequestID: -1,
        allowSDLFunctionalityRequestID: -1,

        onSystemErrorSubscribeRequestID: -1,
        onStatusUpdateSubscribeRequestID: -1,
        onAppPermissionChangedSubscribeRequestID: -1,
        onFileRemovedSubscribeRequestID: -1,
        onAppRegisteredSubscribeRequestID: -1,
        onAppUnregisteredSubscribeRequestID: -1,
        onPlayToneSubscribeRequestID: -1,
        onSDLCloseSubscribeRequestID: -1,
        onSDLConsentNeededSubscribeRequestID: -1,

        onPutFileUnsubscribeRequestID: -1,
        onSystemErrorUnsubscribeRequestID: -1,
        onStatusUpdateUnsubscribeRequestID: -1,
        onAppPermissionChangedUnsubscribeRequestID: -1,
        onFileRemovedUnsubscribeRequestID: -1,
        onAppRegisteredUnsubscribeRequestID: -1,
        onAppUnregisteredUnsubscribeRequestID: -1,
        onPlayToneUnsubscribeRequestID: -1,
        onSDLCloseUnsubscribeRequestID: -1,
        onSDLConsentNeededUnsubscribeRequestID: -1,

        // const
        onSystemErrorNotification: "SDL.OnSystemError",
        onStatusUpdateNotification: "SDL.OnStatusUpdate",
        onAppPermissionChangedNotification: "SDL.OnAppPermissionChanged",
        onPutFileNotification: "BasicCommunication.OnPutFile",
        onFileRemovedNotification: "BasicCommunication.OnFileRemoved",
        onAppRegisteredNotification: "BasicCommunication.OnAppRegistered",
        onAppUnregisteredNotification: "BasicCommunication.OnAppUnregistered",
        onPlayToneNotification: "BasicCommunication.PlayTone",
        onSDLCloseNotification: "BasicCommunication.OnSDLClose",
        onSDLConsentNeededNotification: "SDL.OnSDLConsentNeeded",

        /**
         * init object
         */
        init: function() {

        },

        /**
         * connect to RPC bus
         */
        connect: function() {

            this.client.connect(this, 600); // Magic number is unique identifier
            // for component
        },

        /**
         * disconnect from RPC bus
         */
        disconnect: function() {

            this.client.disconnect();
        },

        /**
         * Client is registered - we can send request starting from this point
         * of time
         */
        onRPCRegistered: function() {

            Em.Logger.log("FFW.BasicCommunicationRPC.onRPCRegistered");
            this._super();

            // subscribe to notifications
            this.onPutFileSubscribeRequestID = this.client
                .subscribeToNotification(this.onPutFileNotification);
            this.onSystemErrorSubscribeRequestID = this.client
                .subscribeToNotification(this.onSystemErrorNotification);
            this.onStatusUpdateSubscribeRequestID = this.client
                .subscribeToNotification(this.onStatusUpdateNotification);
            this.onAppPermissionChangedSubscribeRequestID = this.client
                .subscribeToNotification(this.onAppPermissionChangedNotification);
            this.onFileRemovedSubscribeRequestID = this.client
                .subscribeToNotification(this.onFileRemovedNotification);
            this.onAppRegisteredSubscribeRequestID = this.client
                .subscribeToNotification(this.onAppRegisteredNotification);
            this.onAppUnregisteredSubscribeRequestID = this.client
                .subscribeToNotification(this.onAppUnregisteredNotification);
            this.onPlayToneSubscribeRequestID = this.client
                .subscribeToNotification(this.onPlayToneNotification);
            this.onSDLCloseSubscribeRequestID = this.client
                .subscribeToNotification(this.onSDLCloseNotification);
            this.onSDLConsentNeededSubscribeRequestID = this.client
                .subscribeToNotification(this.onSDLConsentNeededNotification);

        },

        /**
         * Client is unregistered - no more requests
         */
        onRPCUnregistered: function() {

            Em.Logger.log("FFW.BasicCommunicationRPC.onRPCUnregistered");
            this._super();

            // unsubscribe from notifications

            this.onPutFileUnsubscribeRequestID = this.client
                .unsubscribeFromNotification(this.onPutFileNotification);
            this.onSystemErrorUnsubscribeRequestID = this.client
                .unsubscribeFromNotification(this.onSystemErrorNotification);
            this.onStatusUpdateUnsubscribeRequestID = this.client
                .unsubscribeFromNotification(this.onStatusUpdateNotification);
            this.onAppPermissionChangedUnsubscribeRequestID = this.client
                .unsubscribeFromNotification(this.onAppPermissionChangedNotification);
            this.onFileRemovedUnsubscribeRequestID = this.client
                .unsubscribeFromNotification(this.onFileRemovedNotification);
            this.onAppRegisteredUnsubscribeRequestID = this.client
                .unsubscribeFromNotification(this.onAppRegisteredNotification);
            this.onAppUnregisteredUnsubscribeRequestID = this.client
                .unsubscribeFromNotification(this.onAppUnregisteredNotification);
            this.onPlayToneUnsubscribeRequestID = this.client
                .unsubscribeFromNotification(this.onPlayToneUpdatedNotification);
            this.onSDLCloseUnsubscribeRequestID = this.client
                .unsubscribeFromNotification(this.onSDLCloseNotification);
            this.onSDLConsentNeededUnsubscribeRequestID = this.client
                .unsubscribeFromNotification(this.onSDLConsentNeededNotification);
        },

        /**
         * Client disconnected.
         */
        onRPCDisconnected: function() {

            if (SDL.SDLAppController) {
                SDL.SDLAppController.onSDLDisconected();
            }
        },

        /**
         * when result is received from RPC component this function is called It
         * is the propriate place to check results of reuqest execution Please
         * use previously store reuqestID to determine to which request repsonse
         * belongs to
         */
        onRPCResult: function(response) {

            Em.Logger.log("FFW.BasicCommunicationRPC.onRPCResult");
            this._super();

            if (response.result.method == "SDL.GetUserFriendlyMessage") {

                Em.Logger.log("SDL.GetUserFriendlyMessage: Response from SDL!");

                if (response.id in SDL.SDLModel.userFriendlyMessagePull) {
                    var callbackObj = SDL.SDLModel.userFriendlyMessagePull[response.id];
                    callbackObj.callback(response.result.message, callbackObj.appID);
                    SDL.SDLModel.userFriendlyMessagePull.remove(response.id);
                }
            }

            if (response.result.method == "SDL.ActivateApp") {

                Em.Logger.log("SDL.ActivateApp: Response from SDL!");

                if (response.id in SDL.SDLModel.activateAppRequestsList) {

                    var appID = SDL.SDLModel.activateAppRequestsList[response.id];

                    if (!response.result.isSDLAllowed) {

                        var device;

                        if (response.result.device) {
                            device = response.result.device;
                        } else {

                            device = {
                                id: appID,
                                name: SDL.SDLController.getApplicationModel(appID).deviceName
                            };
                        }

                        SDL.SettingsController.AllowSDLFunctionality(device);
                    }

                    if (response.result.isPermissionsConsentNeeded) {

                        this.GetListOfPermissions(appID);

                        this.OnAppPermissionConsent(response.result.allowedFunctions, "GUI", appID);
                    }

                    if (response.result.isAppPermissionsRevoked) {

                        SDL.SettingsController.userFriendlyMessagePopUp();

                        //deleted array
                        SDL.SDLModel.setAppPermissions(params.appRevokedPermissions);
                    }

                    if (response.result.isAppRevoked) {

                        SDL.PopUp.popupActivate("Current version of app is no longer supported!");

                        SDL.SDLModel.onAppUnregistered({
                            "appID": appID
                        });
                    } else {

                        SDL.SDLController.getApplicationModel(appID).turnOnSDL();
                    }

                    delete SDL.SDLModel.activateAppRequestsList[response.id];
                }
            }

            if (response.result.method == "SDL.GetListOfPermissions") {

                Em.Logger.log("SDL.GetListOfPermissions: Response from SDL!");

                SDL.SettingsController.GetListOfPermissionsResponse(response);
            }

            if (response.result.method == "SDL.GetStatusUpdate") {

                Em.Logger.log("SDL.GetStatusUpdate: Response from SDL!");

                SDL.PopUp.popupActivate(response.result);
            }

            if (response.result.method == "SDL.GetURLS") {

                SDL.SDLModel.set('policyURLs', response.result.urls);

                this.OnSystemRequest("PROPRIETARY", response.result.urls[0].policyAppId, SDl.SettingsController.policyUpdateFile, response.result.urls[0].url);
            }
        },

        /**
         * handle RPC erros here
         */
        onRPCError: function(error) {

            Em.Logger.log("FFW.BasicCommunicationRPC.onRPCError");
            this._super();
        },

        /**
         * handle RPC notifications here
         */
        onRPCNotification: function(notification) {

            Em.Logger.log("FFW.BasicCommunicationRPC.onRPCNotification");
            this._super();

            if (notification.method == this.onFileRemovedNotification) {
                SDL.SDLModel.onFileRemoved(notification.params);
            }

            if (notification.method == this.onSystemErrorNotification) {

                var message = "Undefined";

                if (notification.error === "SYNC_REBOOTED") {
                    message = "SDL Core reboot.";
                } else if (notification.error === "SYNC_OUT_OF_MEMMORY") {
                    message = "SDL Core error: out of memory.";
                }

                SDL.PopUp.popupActivate(message);
            }

            if (notification.method == this.onStatusUpdateNotification) {

                //SDL.PopUp.popupActivate(notification.status);

                SDL.TTSPopUp.ActivateTTS(notification.params.status);
            }

            if (notification.method == this.onAppPermissionChangedNotification) {
                SDL.PopUp.popupActivate(response.result);
            }


            if (notification.method == this.onAppRegisteredNotification) {
                SDL.SDLModel.onAppRegistered(notification.params);
                this.OnFindApplications();
            }

            if (notification.method == this.onAppUnregisteredNotification) {
                // remove app from list
                SDL.SDLModel.onAppUnregistered(notification.params);
            }

            if (notification.method == this.onPlayToneNotification) {
                SDL.SDLModel.onPlayTone();
            }

            if (notification.method == this.onSDLCloseNotification) {
                //notification handler method
            }
            if (notification.method == this.onSDLConsentNeededNotification) {

                //Show popUp
                SDL.SettingsController.AllowSDLFunctionality(notification.params.device);

            }
        },

        /**
         * handle RPC requests here
         */
        onRPCRequest: function(request) {

            Em.Logger.log("FFW.BasicCommunicationRPC.onRPCRequest");
            this._super();

            if (this.validationCheck(request)) {

                if (request.method == "BasicCommunication.MixingAudioSupported") {
                    this.MixingAudioSupported(true);
                }
                if (request.method == "BasicCommunication.AllowAllApps") {
                    this.AllowAllApps(true);
                }
                if (request.method == "BasicCommunication.AllowApp") {
                    this.AllowApp(request);
                }
                if (request.method == "BasicCommunication.AllowDeviceToConnect") {
                    this.AllowDeviceToConnect(request.id, request.method, allow);
                }
                if (request.method == "BasicCommunication.UpdateAppList") {
                    if (SDL.States.info.active) {
                        SDL.SDLController
                            .onGetAppList(request.params.applications);
                    }
                    this.sendBCResult(SDL.SDLModel.resultCode["SUCCESS"],
                        request.id,
                        request.method);
                }
                if (request.method == "BasicCommunication.UpdateDeviceList") {
                    SDL.SDLModel.onGetDeviceList(request.params);
                    this.sendBCResult(SDL.SDLModel.resultCode["SUCCESS"],
                        request.id,
                        request.method);
                }
                if (request.method == "BasicCommunication.SystemRequest") {

                    this.OnReceivedPolicyUpdate(SDl.SettingsController.policyUpdateFile);

                    SDl.SettingsController.policyUpdateFile = null;

                    this.sendBCResult(SDL.SDLModel.resultCode["SUCCESS"],
                        request.id,
                        request.method);
                }
                if (request.method == "BasicCommunication.ActivateApp") {

                    if ( SDL.SDLAppController.model && SDL.SDLAppController.model.appID != request.params.appID) {
                        SDL.States.goToStates('info.apps');
                    }

                    SDL.SDLModel.stateLimited = null;

                    SDL.SDLController.getApplicationModel(request.params.appID).turnOnSDL(request.params.appID);
                    this.sendBCResult(SDL.SDLModel.resultCode["SUCCESS"], request.id, request.method);
                }
                if (request.method == "BasicCommunication.GetSystemInfo") {

                    Em.Logger.log("BasicCommunication.GetSystemInfo Response");

                    // send repsonse
                    var JSONMessage = {
                        "jsonrpc": "2.0",
                        "id": request.id,
                        "result": {
                            "code": SDL.SDLModel.resultCode["SUCCESS"], // type (enum) from SDL protocol
                            "method": request.method,
                            "ccpu_version": "ccpu_version",
                            "language": SDL.SDLModel.hmiUILanguage,
                            "wersCountryCode": "wersCountryCode"
                        }
                    };
                    this.client.send(JSONMessage);
                }
                if (request.method == "SDL.GetUserFriendlyMessage") {
                    //TO DO
                    //popUp activation
                }
                if (request.method == "SDL.PolicyUpdate") {
                    SDl.SettingsController.policyUpdateFile = request.params.file;
                    this.GetURLS(7); //Service type for policies

                    this.sendBCResult(SDL.SDLModel.resultCode["SUCCESS"], request.id, request.method);
                }
            }
        },

        /********************* Requests *********************/


        /**
         * Send request if application was activated
         *
         * @param {Number} appID
         */
        ActivateApp: function(appID) {

            var itemIndex = this.client.generateId();

            SDL.SDLModel.activateAppRequestsList[itemIndex] = appID;

            Em.Logger.log("FFW.SDL.OnAppActivated: Request from HMI!");

            // send notification
            var JSONMessage = {
                "jsonrpc": "2.0",
                "id": itemIndex,
                "method": "SDL.ActivateApp",
                "params": {
                    "appID": appID
                }
            };
            this.client.send(JSONMessage);
        },

        /**
         * Send request if application was activated
         *
         * @param {Number} type
         */
        GetURLS: function(type) {

            Em.Logger.log("FFW.SDL.GetURLS: Request from HMI!");

            // send notification
            var JSONMessage = {
                "jsonrpc": "2.0",
                "id": this.client.generateId(),
                "method": "SDL.GetURLS",
                "params": {
                    "service": {
                        "service": type
                    }
                }
            };
            this.client.send(JSONMessage);
        },

        /**
         * Request from HMI to find out Policy Table status
         */
        GetStatusUpdate: function() {

            Em.Logger.log("SDL.GetStatusUpdate: Request from HMI!");

            // send repsonse
            var JSONMessage = {
                "jsonrpc": "2.0",
                "id": this.client.generateId(),
                "method": "SDL.GetStatusUpdate",
                "params": {}
            };
            this.client.send(JSONMessage);
        },

        UpdateSDL: function() {

            Em.Logger.log("SDL.UpdateSDL: Request from HMI!");

            // send repsonse
            var JSONMessage = {
                "jsonrpc": "2.0",
                "id": this.client.generateId(),
                "method": "SDL.UpdateSDL",
                "params": {}
            };
            this.client.send(JSONMessage);
        },

        /**
         * Request to SDLCore to get user friendly message
         * callback function uses text message came in response from SDLCore
         *
         * @callback callbackFunc
         */
        GetUserFriendlyMessage: function(callbackFunc, appID, messageCodes) {

            var itemIndex = this.client.generateId();

            SDL.SDLModel.userFriendlyMessagePull[itemIndex] = {"callbackFunc": callbackFunc, "appID": appID};

            Em.Logger.log("SDL.GetUserFriendlyMessage: Request from HMI!");

            // send repsonse
            var JSONMessage = {
                "jsonrpc": "2.0",
                "id": itemIndex,
                "method": "SDL.GetUserFriendlyMessage",
                "params": {
                    "language": SDL.SDLModel.hmiUILanguage
                }
            };

            if (messageCodes) {
                JSONMessage.params.messageCodes = messageCodes;
            }

            this.client.send(JSONMessage);
        },

        /**
         * Request to SDLCore to get user friendly message
         * callback function uses text message came in response from SDLCore
         *
         * @param {Number} appID
         */
        GetListOfPermissions: function(appID) {

            var itemIndex = this.client.generateId();

            SDL.SDLModel.getListOfPermissionsPull[itemIndex] = appID;

            Em.Logger.log("SDL.GetListOfPermissions: Request from HMI!");

            // send repsonse
            var JSONMessage = {
                "jsonrpc": "2.0",
                "id": itemIndex,
                "method": "SDL.GetListOfPermissions",
                "params": {
                    "appID": appID
                }
            };
            this.client.send(JSONMessage);
        },

        /********************* Responses *********************/

        /**
         * send response from onRPCRequest
         * 
         * @param {Number}
         *            resultCode
         * @param {Number}
         *            id
         * @param {String}
         *            method
         */
        sendBCResult: function(resultCode, id, method) {

            Em.Logger.log("FFW." + method + "Response");

            if (resultCode === SDL.SDLModel.resultCode["SUCCESS"]) {

                // send repsonse
                var JSONMessage = {
                    "jsonrpc": "2.0",
                    "id": id,
                    "result": {
                        "code": resultCode, // type (enum) from SDL protocol
                        "method": method
                    }
                };
                this.client.send(JSONMessage);
            }
        },

        /**
         * Notifies if functionality was changed
         *
         * @param {Boolean}
         *            allowed
         * @param {String}
         *            source
         * @param {String}
         *            device
         */
        OnAllowSDLFunctionality: function(allowed, source, device) {

            Em.Logger.log("FFW.SDL.OnAllowSDLFunctionality");

            // send repsonse
            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "SDL.OnAllowSDLFunctionality",
                "params": {
                    "allowed": allowed,
                    "source": source
                }
            };



            if (device) {
                JSONMessage.params.device = device;
            }

            this.client.send(JSONMessage);
        },


        /**
         * Notifies if language was changed
         *
         * @param {String} lang
         */
        OnSystemInfoChanged: function(lang) {

            Em.Logger.log("FFW.BasicCommunication.OnSystemInfoChanged");

            // send repsonse
            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "BasicCommunication.OnSystemInfoChanged",
                "params": {
                    "language": lang
                }
            };

            this.client.send(JSONMessage);
        },


        /**
         * Notification of decrypted policy table available
         *
         * @param {String} policyfile
         */
        OnReceivedPolicyUpdate: function(policyfile) {

            Em.Logger.log("FFW.SDL.OnReceivedPolicyUpdate");

            // send repsonse
            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "SDL.OnReceivedPolicyUpdate",
                "params": {
                    "policyfile": policyfile
                }
            };

            this.client.send(JSONMessage);
        },

        /**
         * Notifies if functionality was changed
         *
         * @param {Boolean}
         *            allowed
         * @param {String}
         *            source
         * @param {String}
         *            device
         */
        OnAppPermissionConsent: function(consentedFunctions, source, appID) {

            Em.Logger.log("FFW.SDL.OnAppPermissionConsent");

            // send repsonse
            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "SDL.OnAppPermissionConsent",
                "params": {
                    "consentedFunctions": consentedFunctions,
                    "source": source
                }
            };

            if (appID) {
                JSONMessage.params.appID = appID;
            }

            this.client.send(JSONMessage);
        },

        /**
         * send response from onRPCRequest
         * 
         * @param {Number}
         *            id
         * @param {String}
         *            method
         * @param {Boolean}
         *            allow
         */
        AllowDeviceToConnect: function(id, method, allow) {

            Em.Logger.log("FFW." + method + "Response");

            // send repsonse
            var JSONMessage = {
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "code": SDL.SDLModel.resultCode["SUCCESS"], // type (enum)
                    // from SDL
                    // protocol
                    "method": method,
                    "allow": true
                }
            };
            this.client.send(JSONMessage);

        },

        /**
         * notification that UI is ready BasicCommunication should be sunscribed
         * to this notification
         */
        onReady: function() {

            Em.Logger.log("FFW.BasicCommunication.onReady");

            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "BasicCommunication.OnReady"
            };
            this.client.send(JSONMessage);
        },

/**
         * Sent notification to SDL when HMI closes
         */
        OnIgnitionCycleOver: function() {

            Em.Logger.log("FFW.BasicCommunication.OnIgnitionCycleOver");

            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "BasicCommunication.OnIgnitionCycleOver"
            };
            this.client.send(JSONMessage);
        },

        /**
         * Send request if application was activated
         * 
         * @param {number} appID
         */
        OnAppActivated: function(appID) {

            Em.Logger.log("FFW.BasicCommunication.OnAppActivated");

            // send notification
            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "BasicCommunication.OnAppActivated",
                "params": {
                    "appID": appID
                }
            };
            this.client.send(JSONMessage);
        },

        /**
         * This methos is request to get list of registered apps.
         */
        OnFindApplications: function() {

            Em.Logger.log("FFW.BasicCommunication.OnFindApplications");

            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "BasicCommunication.OnFindApplications"
            };

            if (SDL.SDLModel.CurrDeviceInfo.name
                || SDL.SDLModel.CurrDeviceInfo.id) {

                JSONMessage.params = {
                    "deviceInfo": SDL.SDLModel.CurrDeviceInfo
                };
            }

            this.client.send(JSONMessage);
        },

        /**
         * This methos is request to get list of registered apps.
         */
        AddStatisticsInfo: function(statisticType) {

            Em.Logger.log("FFW.SDL.AddStatisticsInfo");

            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "SDL.AddStatisticsInfo",
                "params": {
                    "statisticType": statisticType
                }
            };

            this.client.send(JSONMessage);
        },

        /**
         * Request for list of avaliable devices
         */
        getDeviceList: function() {

            Em.Logger.log("FFW.BasicCommunication.GetDeviceList");

            this.getDeviceListRequestID = this.client.generateID();

            var JSONMessage = {
                "id": this.getDeviceListRequestID,
                "jsonrpc": "2.0",
                "method": "BasicCommunication.GetDeviceList"
            };
            this.client.send(JSONMessage);
        },

        /**
         * Invoked by UI component when user switches to any functionality which
         * is not other mobile application.
         * 
         * @params {String}
         * @params {Number}
         */
        OnAppDeactivated: function(reason, appID) {

            Em.Logger.log("FFW.BasicCommunication.OnAppDeactivated");

            // send request

            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "BasicCommunication.OnAppDeactivated",
                "params": {
                    "appID": appID,
                    "reason": reason
                }
            };
            this.client.send(JSONMessage);
        },

        /**
         * Initiated by HMI user. In response optional list of found devices -
         * if not provided, not were found.
         */
        OnStartDeviceDiscovery: function() {

            Em.Logger.log("FFW.BasicCommunication.OnStartDeviceDiscovery");

            // send request

            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "BasicCommunication.OnStartDeviceDiscovery"
            };
            this.client.send(JSONMessage);
        },

        /**
         * Used by HMI when User chooses to exit application.
         * 
         * @params {Number}
         */
        ExitApplication: function(appID) {

            Em.Logger.log("FFW.BasicCommunication.OnExitApplication");

            // send request

            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "BasicCommunication.OnExitApplication",
                "params": {
                    "appID": appID
                }
            };
            this.client.send(JSONMessage);
        },

        /**
         * Sent by HMI to SDL to close all registered applications.
         * 
         * @params {String}
         */
        ExitAllApplications: function(reason) {

            Em.Logger.log("FFW.BasicCommunication.OnExitAllApplications");

            // send request

            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "BasicCommunication.OnExitAllApplications",
                "params": {
                    "reason": reason
                }
            };
            this.client.send(JSONMessage);
        },

        /**
         * Response with params of the last one supports mixing audio (ie
         * recording TTS command and playing audio).
         * 
         * @params {Number}
         */
        MixingAudioSupported: function(attenuatedSupported) {

            Em.Logger.log("FFW.BasicCommunication.MixingAudioSupportedResponse");

            // send request

            var JSONMessage = {
                "id": this.client.idStart,
                "jsonrpc": "2.0",
                "result": {
                    "code": 0,
                    "attenuatedSupported": attenuatedSupported,
                    "method": "BasicCommunication.MixingAudioSupported"
                }
            };
            this.client.send(JSONMessage);
        },

        /**
         * Response with Results by user/HMI allowing SDL functionality or
         * disallowing access to all mobile apps.
         * 
         * @params {Number}
         */
        AllowAllApps: function(allowed) {

            Em.Logger.log("FFW.BasicCommunication.AllowAllAppsResponse");

            // send request

            var JSONMessage = {
                "id": this.client.idStart,
                "jsonrpc": "2.0",
                "result": {
                    "code": 0,
                    "method": "BasicCommunication.AllowAllApps",
                    "allowed": allowed
                }
            };
            this.client.send(JSONMessage);
        },

        /**
         * Response with result of allowed application
         * 
         * @params {Number}
         */
        AllowApp: function(request) {

            Em.Logger.log("FFW.BasicCommunication.AllowAppResponse");

            var allowedFunctions = [];
            request.params.appPermissions.forEach(function(entry) {
                    allowedFunctions.push(
                        {
                            name: entry,
                            allowed: true
                        }
                    )
                }
            );

            // send request

            var JSONMessage = {
                "id": request.id,
                "jsonrpc": "2.0",
                "result": {
                    "code": 0,
                    "method": "BasicCommunication.AllowApp",
                    "allowedFunctions": allowedFunctions
                }
            };
            this.client.send(JSONMessage);
        },

        /**
         * Notifies if device was choosed
         * 
         * @param {String}
         *            deviceName
         * @param {Number}
         *            appID
         */
        OnDeviceChosen: function(deviceName, appID) {

            Em.Logger.log("FFW.BasicCommunication.OnDeviceChosen");

            // send repsonse
            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "BasicCommunication.OnDeviceChosen",
                "params": {
                    "deviceInfo": {
                        "name": deviceName,
                        "id": appID
                    }
                }
            };
            this.client.send(JSONMessage);
        },

        /**
         * Send error response from onRPCRequest
         * 
         * @param {Number}
         *            resultCode
         * @param {Number}
         *            id
         * @param {String}
         *            method
         */
        sendError: function(resultCode, id, method, message) {

            Em.Logger.log("FFW." + method + "Response");

            if (resultCode != SDL.SDLModel.resultCode["SUCCESS"]) {

                // send repsonse
                var JSONMessage = {
                    "jsonrpc": "2.0",
                    "id": id,
                    "error": {
                        "code": resultCode, // type (enum) from SDL protocol
                        "message": message,
                        "data": {
                            "method": method
                        }
                    }
                };
                this.client.send(JSONMessage);
            }
        },

        /**
         * Initiated by HMI.
         */
        OnSystemRequest: function(type, appID, fileName, utl) {

            Em.Logger.log("FFW.BasicCommunication.OnSystemRequest");

            // send request

            var JSONMessage = {
                "jsonrpc": "2.0",
                "method": "BasicCommunication.OnSystemRequest",
                "params":{
                    "requestType": type,
                    "url": ["http://127.0.0.1"],
                    "fileType": "JSON",
                    "offset": 1000,
                    "length": 10000,
                    "timeout": 500,
                    "fileName": fileName ? fileName : document.location.pathname.replace("index.html", "IVSU/PROPRIETARY_REQUEST"),
                    "appID": SDL.SDLAppController.model ? SDL.SDLAppController.model.appID : null
                }
            };
            this.client.send(JSONMessage);
        }

    })
