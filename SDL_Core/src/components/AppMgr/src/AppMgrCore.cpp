//
// Copyright (c) 2013, Ford Motor Company
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following
// disclaimer in the documentation and/or other materials provided with the
// distribution.
//
// Neither the name of the Ford Motor Company nor the names of its contributors
// may be used to endorse or promote products derived from this software
// without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

#include <sys/socket.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>

#include "LoggerHelper.hpp"
#include "Utils/WorkWithOS.h"
#include "AppMgr/AppMgrCore.h"
#include "AppMgr/AppPolicy.h"
#include "AppMgr/RegistryItem.h"
#include "AppMgr/AppMgrCoreQueues.h"
#include "AppMgr/HMIHandler.h"
#include "AppMgr/MobileHandler.h"
#include "AppMgr/ConnectionHandler.h"
#include "AppMgr/Application_v2.h"
#include "AppMgr/Application_v1.h"
#include "JSONHandler/SDLRPCMessage.h"
#include "JSONHandler/SDLRPCRequest.h"
#include "JSONHandler/SDLRPCResponse.h"
#include "JSONHandler/SDLRPCNotification.h"
#include "JSONHandler/SDLRPCObjects/V1/Marshaller.h"
#include "JSONHandler/SDLRPCObjects/V2/Marshaller.h"
#include "JSONHandler/JSONHandler.h"
#include "JSONHandler/JSONRPC2Handler.h"
#include "JSONHandler/RPC2Objects/Marshaller.h"
#include "JSONHandler/RPC2Command.h"
#include "JSONHandler/RPC2Request.h"
#include "JSONHandler/RPC2Response.h"
#include "JSONHandler/RPC2Notification.h"
#include "JSONHandler/SDLRPCObjects/V2/AppType.h"
#include "JSONHandler/SDLRPCObjects/V2/VehicleDataType.h"

#include "ConnectionHandler/CConnectionHandler.hpp"

namespace {
    // We wait for some request form HMI:
    // UI::GetCapabilities, VR::GetCapabilities, TTS::GetCapabilities, Buttons::GetCapabilities
    // VehicleInfo::GetVehicleType, UI::GetSupportedLanguages, TTS::GetSupportedLanguages
    // VR::GetSupportedLanguages, UI::GetLanguage, VR::GetLanguage, TTS::GetLanguage
    // Look for METHOD_NSRPC2COMMUNICATION_UI__ONREADY for details
    const int HMI_STARTUP_REQUEST_COUNT = 11;

    template<typename To, typename From, typename InternalType>
    void convert(const std::vector<From> & from, std::vector<To> & result)
    {
        typename std::vector<From>::const_iterator i = from.begin();
        for(i; i != from.end(); ++i)
        {
            const From& caps = *i;
            To item;
            item.set(static_cast<InternalType>(caps.get()));
            result.push_back(item);
        }
    }

    template<typename Response>
    void sendResponse(int responseId, NsSmartDeviceLinkRPCV2::Result::ResultInternal result)
    {
        Response* response = new Response;
        if (!response)
            return;

        response->setId(responseId);
        response->setResult(result);
        NsAppManager::HMIHandler::getInstance().sendResponse(response);
    }

    template<typename Response, typename Result>
    void sendResponse(NsSmartDeviceLinkRPCV2::FunctionID::FunctionIDInternal functionId
        , Result result
        , unsigned int correlationID
        , bool succes
        , unsigned int sessionKey)
    {
        Response* response = new Response;
        if (!response)
            return;

        response->setMethodId(functionId);
        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
        response->set_success(succes);
        response->set_resultCode(result);
        response->setCorrelationID(correlationID);
        NsAppManager::MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
    }

    template<typename Response, typename Result>
    void sendResponse(NsSmartDeviceLinkRPC::SDLRPCMessage * request
        , Result result
        , bool succes
        , unsigned int sessionKey)
    {
        Response* response = new Response;
        if (!response)
            return;

        response->setMethodId(request->getMethodId());
        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
        response->setCorrelationID(request->getCorrelationID());
        response->set_success(succes);
        response->set_resultCode(result);
        NsAppManager::MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
    }

    template<typename Response>
    NsAppManager::Application_v2* getApplicationV2AndCheckHMIStatus(unsigned int sessionKey
        , NsSmartDeviceLinkRPC::SDLRPCMessage * request)
    {
        NsAppManager::Application_v2* app = static_cast<NsAppManager::Application_v2*>(
            NsAppManager::AppMgrCore::getInstance().getItem(sessionKey));
        if(!app)
        {
            /*LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                << " hasn't been associated with any application!");*/

            sendResponse<Response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                request
                , NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED
                , false
                , sessionKey);

            return NULL;
        }

        if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
        {
            /*LOG4CPLUS_ERROR_EXT(mLogger, "An application " << app->getName() << " with session key "
                << sessionKey << " has not been activated yet!" );*/

            sendResponse<Response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                request
                , NsSmartDeviceLinkRPCV2::Result::REJECTED
                , false
                , sessionKey);

            return NULL;
        }

        return app;
    }

    pthread_t sendingThread;
    struct thread_data
    {
        int timeout;
        std::string url;
        NsAppManager::SyncPManager::PData pdata;
    };

    void *SendPData(void *data)
    {
        log4cplus::Logger logger = log4cplus::Logger::getInstance(LOG4CPLUS_TEXT("AppMgrCore"));
        LOG4CPLUS_INFO_EXT(logger, " Started data sending thread");
        struct thread_data* my_data = (struct thread_data*) data;
        int timeout = my_data->timeout;
        std::string url = my_data->url;
        NsAppManager::SyncPManager::PData pData = my_data->pdata;
        LOG4CPLUS_INFO_EXT(logger, " Sending params: url " << url << " timeout " << timeout << " data of " << pData.size() << " lines");
        sleep(timeout);  // TODO(AK): Why we use it?
        int port = 80;
        size_t pos = url.find(":");
        if(pos != std::string::npos)
        {
            std::string strPort = url.substr(pos+1);
            if(!strPort.empty())
            {
                port = atoi(strPort.c_str());
            }
        }
        std::string host = url.substr(0, pos);
        LOG4CPLUS_INFO_EXT(logger, " Sending at " << host << " port " << port);
        // TODO (pv): change implementation.
        LOG4CPLUS_INFO_EXT(logger, " All data sent to host " << host << " port " << port);
        pthread_exit(NULL);
    }

    // !----------------------------------------------------------------------------------------------------------------

    const unsigned int AUDIO_PASS_THRU_TIMEOUT = 1;

    pthread_cond_t cv;
    pthread_mutex_t audioPassThruMutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_t audioPassThruThread;

    // Don't send PerformAudioPassThru_response to mobile if false.
    bool sendEndAudioPassThruToHMI(unsigned int sessionKey, unsigned int correlationID)
    {
        // Send respons to HMI.
        NsRPC2Communication::UI::EndAudioPassThru* endAudioPassThru
            = new NsRPC2Communication::UI::EndAudioPassThru;
        if (!endAudioPassThru)
        {
            std::cout << "OUT_OF_MEMORY: new NsRPC2Communication::UI::EndAudioPassThru." << std::endl;
            sendResponse<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response
                , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(NsSmartDeviceLinkRPCV2::FunctionID::PerformAudioPassThruID
                    , NsSmartDeviceLinkRPCV2::Result::OUT_OF_MEMORY
                    , correlationID
                    , false
                    , sessionKey);
            return false;
        }

        NsAppManager::Application* app = NsAppManager::AppMgrCore::getInstance().getItem(sessionKey);
        if(!app)
        {
            std::cout << "No application associated with this registry item!" << std::endl;
            return false;
        }

        endAudioPassThru->setId(NsAppManager::HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
        endAudioPassThru->set_appId(app->getAppID());
        NsAppManager::HMIHandler::getInstance().sendRequest(endAudioPassThru);
        return true;
    }

    void AudioPassThruTimerProc(int i)
    {
        pthread_cond_signal(&cv);
    }

    struct AudioPassThruData
    {
        unsigned int sessionKey;  // For error reports
        unsigned int id;  // For error reports

        unsigned int maxDuration;
        NsSmartDeviceLinkRPCV2::SamplingRate samplingRate;
        NsSmartDeviceLinkRPCV2::AudioCaptureQuality bitsPerSample;
        NsSmartDeviceLinkRPCV2::AudioType audioType;
    };

    void* AudioPassThru(void* data)
    {
        AudioPassThruData* data_ = static_cast<AudioPassThruData*>(data);
        if (!data_)
        {
            NsAppManager::AppMgrCore::getInstance().setAudioPassThruFlag(false);
            std::cout << "AudioPassThruData empty." << std::endl;
            pthread_exit(NULL);
        }

        unsigned int audioLength = 0;
        std::string filename;
        if (data_->bitsPerSample.get() == NsSmartDeviceLinkRPCV2::AudioCaptureQuality::FIX_8_BIT)
        {
            filename = "audio.8bit.wav";
            audioLength = 5000;
        }
        else if (data_->bitsPerSample.get() == NsSmartDeviceLinkRPCV2::AudioCaptureQuality::FIX_16_BIT)
        {
            filename = "";  // TODO(AK): Add file name here.
            audioLength = static_cast<unsigned int>(1000 * 60 * 2.7); // 3 minute audio.
        }
        else
        {
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            if (sendEndAudioPassThruToHMI(data_->sessionKey, data_->id))
            {
                sendResponse<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response
                    , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(NsSmartDeviceLinkRPCV2::FunctionID::PerformAudioPassThruID
                    , NsSmartDeviceLinkRPCV2::Result::GENERIC_ERROR
                    , data_->id
                    , false
                    , data_->sessionKey);
            }

            NsAppManager::AppMgrCore::getInstance().setAudioPassThruFlag(false);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            pthread_exit(NULL);
        }

        std::vector<unsigned char> binaryData;
        if (!WorkWithOS::readFileAsBinary(filename, binaryData))
        {
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            std::cout << "Can't read from file." << std::endl;
            if (sendEndAudioPassThruToHMI(data_->sessionKey, data_->id))
            {
                sendResponse<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response
                    , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(NsSmartDeviceLinkRPCV2::FunctionID::PerformAudioPassThruID
                    , NsSmartDeviceLinkRPCV2::Result::GENERIC_ERROR
                    , data_->id
                    , false
                    , data_->sessionKey);
            }

            NsAppManager::AppMgrCore::getInstance().setAudioPassThruFlag(false);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            pthread_exit(NULL);
        }

        if (binaryData.empty())
        {
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            std::cout << "Binary data empty." << std::endl;
            if (sendEndAudioPassThruToHMI(data_->sessionKey, data_->id))
            {
                sendResponse<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response
                    , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(NsSmartDeviceLinkRPCV2::FunctionID::PerformAudioPassThruID
                    , NsSmartDeviceLinkRPCV2::Result::GENERIC_ERROR
                    , data_->id
                    , false
                    , data_->sessionKey);
            }

            NsAppManager::AppMgrCore::getInstance().setAudioPassThruFlag(false);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            pthread_exit(NULL);
        }

        unsigned int percentOfAudioLength = 0;  // % of audio file.
        unsigned int dataLength = 0;  // part of file data.

        // Send only part of file
        if (data_->maxDuration != 0 && data_->maxDuration < audioLength)
        {
            percentOfAudioLength = (data_->maxDuration * 100) / audioLength;
            dataLength = (binaryData.size() * percentOfAudioLength) / 100;
        }
        else
        {
            percentOfAudioLength = 100;
            dataLength = binaryData.size();
        }

        // Part of file in seconds = audio length in seconds * (%) of audio length / 100%
        unsigned int seconds = ((audioLength / 1000) * percentOfAudioLength) / 100;
        // Step is data length * AUDIO_PASS_THRU_TIMEOUT / seconds
        unsigned int step = dataLength * AUDIO_PASS_THRU_TIMEOUT / seconds;

        std::vector<unsigned char>::iterator from = binaryData.begin();
        std::vector<unsigned char>::iterator to = from + step;

        for (int i = 0; i != seconds; i += AUDIO_PASS_THRU_TIMEOUT)  // minimal timeout is 1 sec now.
        {
            struct itimerval tout_val;
            tout_val.it_interval.tv_sec = 0;
            tout_val.it_interval.tv_usec = 0;
            tout_val.it_value.tv_sec = AUDIO_PASS_THRU_TIMEOUT;
            tout_val.it_value.tv_usec = 0;
            setitimer(ITIMER_REAL, &tout_val, 0);
            signal(SIGALRM, AudioPassThruTimerProc);

            pthread_cond_wait(&cv, &audioPassThruMutex);

            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            NsSmartDeviceLinkRPCV2::OnAudioPassThru* onAudioPassThru = new NsSmartDeviceLinkRPCV2::OnAudioPassThru;
            if (!onAudioPassThru)
            {
                std::cout << "OUT_OF_MEMORY: new NsSmartDeviceLinkRPCV2::OnAudioPassThru." << std::endl;
                if (sendEndAudioPassThruToHMI(data_->sessionKey, data_->id))
                {
                    sendResponse<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response
                        , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(NsSmartDeviceLinkRPCV2::FunctionID::PerformAudioPassThruID
                        , NsSmartDeviceLinkRPCV2::Result::OUT_OF_MEMORY
                        , data_->id
                        , false
                        , data_->sessionKey);
                }

                delete onAudioPassThru;
                NsAppManager::AppMgrCore::getInstance().setAudioPassThruFlag(false);
                pthread_exit(NULL);
            }

            onAudioPassThru->setBinaryData(std::vector<unsigned char>(from, to));
            onAudioPassThru->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnAudioPassThruID);
            onAudioPassThru->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);

            NsAppManager::MobileHandler::getInstance().sendRPCMessage(onAudioPassThru, data_->sessionKey);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

            from = to;
            to = to + step;
        }

        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        //sendEndAudioPassThruToHMI(data_->sessionKey);
        /*if (sendEndAudioPassThruToHMI(data_->sessionKey))
        {
            // Send response to mobile.
            sendResponse<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response
                , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(NsSmartDeviceLinkRPCV2::FunctionID::PerformAudioPassThruID
                , NsSmartDeviceLinkRPCV2::Result::SUCCESS
                , NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE
                , true
                , data_->sessionKey);
        }*/

        NsAppManager::AppMgrCore::getInstance().setAudioPassThruFlag(false);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_exit(NULL);
    }
}

namespace NsAppManager
{
    log4cplus::Logger AppMgrCore::mLogger = log4cplus::Logger::getInstance(LOG4CPLUS_TEXT("AppMgrCore"));

    /**
     * \brief Returning class instance
     * \return class instance
     */
    AppMgrCore& AppMgrCore::getInstance( )
    {
        static AppMgrCore appMgr;
        return appMgr;
    }

    /**
     * \brief Default class constructor
     */
    AppMgrCore::AppMgrCore()
        : mQueueRPCSmartDeviceLinkObjectsIncoming(new AppMgrCoreQueue<Message>(&AppMgrCore::handleMobileRPCMessage, this))
        , mQueueRPCBusObjectsIncoming(new AppMgrCoreQueue<NsRPC2Communication::RPC2Command*>(&AppMgrCore::handleBusRPCMessageIncoming, this))
        , mDriverDistractionV1(0)
        , mDriverDistractionV2(0)
        , mAudioPassThruFlag(false)
        , mPerformInteractionFlag(-1)
        , mHMIStartupFlag(HMI_STARTUP_REQUEST_COUNT)
    {
        LOG4CPLUS_INFO_EXT(mLogger, " AppMgrCore constructed!");

        pthread_mutex_init(&audioPassThruMutex, NULL);
        memset(static_cast<void*>(&cv), 0, sizeof(cv));
    }

    /**
     * \brief Copy constructor
     */
    AppMgrCore::AppMgrCore(const AppMgrCore &)
        :mQueueRPCSmartDeviceLinkObjectsIncoming(0)
        ,mQueueRPCBusObjectsIncoming(0)
        ,mDriverDistractionV1(0)
        ,mDriverDistractionV2(0)
    {
    }

    /**
     * \brief Default class destructor
     */
    AppMgrCore::~AppMgrCore()
    {
        if(mQueueRPCSmartDeviceLinkObjectsIncoming)
            delete mQueueRPCSmartDeviceLinkObjectsIncoming;
        if(mQueueRPCBusObjectsIncoming)
            delete mQueueRPCBusObjectsIncoming;
        if(mDriverDistractionV1)
            delete mDriverDistractionV1;
        if(mDriverDistractionV2)
            delete mDriverDistractionV2;

        LOG4CPLUS_INFO_EXT(mLogger, " AppMgrCore destructed!");
    }

    /**
     * \brief push mobile RPC message to a queue
     * \param message a message to be pushed
     * \param connectionID id of a connection associated with application that sent the message
     * \param sessionID an id of a session associated with the application which pushes a message
     */
    void AppMgrCore::pushMobileRPCMessage( NsSmartDeviceLinkRPC::SDLRPCMessage * message, int appId )
    {
        LOG4CPLUS_INFO_EXT(mLogger, " Pushing mobile RPC message " << message->getMethodId() << " for application id " << appId << "...");
        if(!message)
        {
            LOG4CPLUS_ERROR_EXT(mLogger, "Nothing to push! A null-ptr occured!");
            return;
        }

        mQueueRPCSmartDeviceLinkObjectsIncoming->pushMessage(Message(message, appId));

        LOG4CPLUS_INFO_EXT(mLogger, " Pushed mobile RPC message " << message->getMethodId() << " for application id " << appId);
    }

    /**
     * \brief push HMI RPC2 message to a queue
     * \param message a message to be pushed
     */
    void AppMgrCore::pushRPC2CommunicationMessage( NsRPC2Communication::RPC2Command * message )
    {
        LOG4CPLUS_INFO_EXT(mLogger, " Returning a message " << message->getMethod() << " from HMI...");
        if(!message)
        {
            LOG4CPLUS_ERROR_EXT(mLogger, "Nothing to push! A null-ptr occured!");
            return;
        }

        mQueueRPCBusObjectsIncoming->pushMessage(message);

        LOG4CPLUS_INFO_EXT(mLogger, " Returned a message " << message->getMethod() << " from HMI");
    }

    /**
     * \brief method to execute threads.
     */
    void AppMgrCore::executeThreads()
    {
        LOG4CPLUS_INFO_EXT(mLogger, " Threads are being started!");

        mQueueRPCSmartDeviceLinkObjectsIncoming->executeThreads();
        mQueueRPCBusObjectsIncoming->executeThreads();

        LOG4CPLUS_INFO_EXT(mLogger, " Threads have been started!");
    }

    /**
     * \brief mobile RPC message handler
     * \param mesage a message to be handled
     * \param pThis a pointer to AppMgrCore class instance
     */
    void AppMgrCore::handleMobileRPCMessage(Message message , void *pThis)
    {
        int sessionKey = message.second;

        NsSmartDeviceLinkRPC::SDLRPCMessage* mobileMsg = message.first;
        if(!mobileMsg)
        {
            LOG4CPLUS_ERROR_EXT(mLogger, " No message associated with the session key " << sessionKey << " !");
            return;
        }
        LOG4CPLUS_INFO_EXT(mLogger, " A mobile RPC message " << mobileMsg->getMethodId() << " has been received for the session key " << sessionKey << " !");
        if(!pThis)
        {
            LOG4CPLUS_ERROR_EXT(mLogger, " pThis should point to an instance of AppMgrCore class");
            return;
        }
        AppMgrCore* core = (AppMgrCore*)pThis;
        const unsigned int& protocolVersion = mobileMsg->getProtocolVersion();
        /*const NsConnectionHandler::tDeviceHandle& currentDeviceHandle = core->mDeviceHandler.findDeviceAssignedToSession(sessionKey);
        const NsConnectionHandler::CDevice* currentDevice = core->mDeviceList.findDeviceByHandle(currentDeviceHandle);
        if(!currentDevice)
        {
            LOG4CPLUS_ERROR_EXT(mLogger, " Cannot retreive current device name for the message with session key " << sessionKey << " !");
            return;
        }
        const std::string& currentDeviceName = currentDevice->getUserFriendlyName();*/

        LOG4CPLUS_INFO_EXT(mLogger, "Message received is from protocol " << protocolVersion);
        if ( 1 == mobileMsg->getProtocolVersion() )
        {
            switch(mobileMsg->getMethodId())
            {
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_REGISTERAPPINTERFACE_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A RegisterAppInterface request has been invoked");

                    NsSmartDeviceLinkRPC::RegisterAppInterface_request * object = (NsSmartDeviceLinkRPC::RegisterAppInterface_request*)mobileMsg;
                    NsSmartDeviceLinkRPC::RegisterAppInterface_response* response = new NsSmartDeviceLinkRPC::RegisterAppInterface_response();
                    const std::string& appName = object->get_appName();
                    response->set_success(true);
                    response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                    response->setCorrelationID(object->getCorrelationID());
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);

                    if(core->getItem(sessionKey))
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " Application " << appName << " is already registered!");
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_REGISTERED_ALREADY);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    Application_v1* app = (Application_v1*)core->registerApplication( object, sessionKey );
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " Application " << appName << " hasn't been registered!");
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    std::vector<NsSmartDeviceLinkRPC::ButtonCapabilities> buttonCapsResult;
                    std::vector<NsSmartDeviceLinkRPCV2::ButtonCapabilities> buttonCapsOrig = core->mButtonCapabilitiesV2.get();
                    for ( int i = 0; i < buttonCapsOrig.size(); ++i )
                    {
                        NsSmartDeviceLinkRPC::ButtonCapabilities buttonCaps;
                        buttonCaps.set_longPressAvailable(buttonCapsOrig[i].get_longPressAvailable());
                        buttonCaps.set_name(
                            static_cast<NsSmartDeviceLinkRPC::ButtonName::ButtonNameInternal>(
                                buttonCapsOrig[i].get_name().get()));
                        buttonCaps.set_shortPressAvailable(buttonCapsOrig[i].get_shortPressAvailable());
                        buttonCaps.set_upDownAvailable(buttonCapsOrig[i].get_upDownAvailable());
                        buttonCapsResult.push_back(buttonCaps);
                    }
                    response->set_buttonCapabilities(buttonCapsResult);

                    std::vector<NsSmartDeviceLinkRPC::HmiZoneCapabilities> hmiCapsResult;
                    convert<NsSmartDeviceLinkRPC::HmiZoneCapabilities,
                            NsSmartDeviceLinkRPCV2::HmiZoneCapabilities,
                            NsSmartDeviceLinkRPC::HmiZoneCapabilities::HmiZoneCapabilitiesInternal>(
                        core->mHmiZoneCapabilitiesV2.get(),
                        hmiCapsResult);
                    response->set_hmiZoneCapabilities(hmiCapsResult);

                    std::vector<NsSmartDeviceLinkRPC::SpeechCapabilities> speechCapsResult;
                    convert<NsSmartDeviceLinkRPC::SpeechCapabilities,
                            NsSmartDeviceLinkRPCV2::SpeechCapabilities,
                            NsSmartDeviceLinkRPC::SpeechCapabilities::SpeechCapabilitiesInternal>(
                        core->mSpeechCapabilitiesV2.get(),
                        speechCapsResult);
                    response->set_speechCapabilities(speechCapsResult);

                    std::vector<NsSmartDeviceLinkRPC::VrCapabilities> vrCapsResult;
                    convert<NsSmartDeviceLinkRPC::VrCapabilities,
                            NsSmartDeviceLinkRPCV2::VrCapabilities,
                            NsSmartDeviceLinkRPC::VrCapabilities::VrCapabilitiesInternal>(
                        core->mVrCapabilitiesV2.get(),
                        vrCapsResult);
                    response->set_vrCapabilities(vrCapsResult);

                    response->set_displayCapabilities(core->mDisplayCapabilitiesV1);
                    response->set_language(core->mUiLanguageV1);
                    if (object->get_languageDesired().get() != core->mUiLanguageV1.get())
                    {
                        LOG4CPLUS_WARN(mLogger, "Wrong language on registering application " << appName);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::WRONG_LANGUAGE);
                    }
                    response->set_syncMsgVersion(app->getSyncMsgVersion());


                    LOG4CPLUS_INFO_EXT(mLogger, " A RegisterAppInterface response for the app "  << app->getName()
                        << " connection/session key " << app->getAppID()
                        << " gets sent to a mobile side... ");
                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);

                    NsRPC2Communication::BasicCommunication::OnAppRegistered* appRegistered = new NsRPC2Communication::BasicCommunication::OnAppRegistered();
                    NsSmartDeviceLinkRPCV2::HMIApplication hmiApp;
                    hmiApp.set_appName(app->getName());
                    hmiApp.set_appId(app->getAppID());
                    hmiApp.set_isMediaApplication(app->getIsMediaApplication());

                    std::map<int,DeviceStorage>::const_iterator it = core->mDevices.find( app->getDeviceHandle() );
                    std::string currentDeviceName = "";
                    if ( core->mDevices.end() != it )
                    {
                        currentDeviceName = it->second.getUserFriendlyName();
                    }

                    hmiApp.set_deviceName(currentDeviceName);
                    hmiApp.set_hmiDisplayLanguageDesired(static_cast<NsSmartDeviceLinkRPCV2::Language::LanguageInternal>(app->getLanguageDesired().get()));
                    hmiApp.set_languageDesired(static_cast<NsSmartDeviceLinkRPCV2::Language::LanguageInternal>(app->getLanguageDesired().get()));

                    appRegistered->set_application(hmiApp);
                    HMIHandler::getInstance().sendNotification(appRegistered);
                    LOG4CPLUS_INFO_EXT(mLogger, " An SmartDeviceLinkCore::OnAppRegistered notofocation for the app " << app->getName()
                        << " application id " << app->getAppID()
                        << " gets sent to an HMI side... ");
                    LOG4CPLUS_INFO_EXT(mLogger, " A RegisterAppInterface request was successful: registered an app " << app->getName()
                        << " application id " << app->getAppID());

                    NsSmartDeviceLinkRPC::OnHMIStatus* status = new NsSmartDeviceLinkRPC::OnHMIStatus();
                    status->set_hmiLevel(
                        static_cast<NsSmartDeviceLinkRPC::HMILevel::HMILevelInternal>(app->getApplicationHMIStatusLevel()));
                    status->set_audioStreamingState(
                        static_cast<NsSmartDeviceLinkRPC::AudioStreamingState::AudioStreamingStateInternal>(
                            app->getApplicationAudioStreamingState().get()));
                    status->set_systemContext(
                        static_cast<NsSmartDeviceLinkRPC::SystemContext::SystemContextInternal>(app->getSystemContext().get()));
                    MobileHandler::getInstance().sendRPCMessage(status, sessionKey);
                    LOG4CPLUS_INFO_EXT(mLogger, " An OnHMIStatus notification for the app "  << app->getName()
                        << " connection/session key " << app->getAppID()
                        << " gets sent to a mobile side... ");

                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_UNREGISTERAPPINTERFACE_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An UnregisterAppInterface request has been invoked");

                    NsSmartDeviceLinkRPC::UnregisterAppInterface_request * object = (NsSmartDeviceLinkRPC::UnregisterAppInterface_request*)mobileMsg;
                    Application* app = core->getItem(sessionKey);
                    NsSmartDeviceLinkRPC::UnregisterAppInterface_response* response = new NsSmartDeviceLinkRPC::UnregisterAppInterface_response();
                    response->setCorrelationID(object->getCorrelationID());
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    std::string appName = app->getName();
                    int appId = app->getAppID();
                    //core->removeAppFromHmi(app, sessionKey);
                    core->unregisterApplication( sessionKey );

                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->set_success(true);
                    response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);

                    /*NsSmartDeviceLinkRPC::OnAppInterfaceUnregistered* msgUnregistered = new NsSmartDeviceLinkRPC::OnAppInterfaceUnregistered();
                    msgUnregistered->set_reason(NsSmartDeviceLinkRPC::AppInterfaceUnregisteredReason(NsSmartDeviceLinkRPC::AppInterfaceUnregisteredReason::USER_EXIT));
                    MobileHandler::getInstance().sendRPCMessage(msgUnregistered, sessionKey);*/

                    LOG4CPLUS_INFO_EXT(mLogger, " An application " << appName << " has been unregistered successfully ");
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_SUBSCRIBEBUTTON_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A SubscribeButton request has been invoked");

                    NsSmartDeviceLinkRPC::SubscribeButton_request * object = (NsSmartDeviceLinkRPC::SubscribeButton_request*)mobileMsg;
                    NsSmartDeviceLinkRPC::SubscribeButton_response* response = new NsSmartDeviceLinkRPC::SubscribeButton_response();
                    //RegistryItem* item = AppMgrRegistry::getInstance().getItem(sessionKey);
                    response->setCorrelationID(object->getCorrelationID());
                    /*if(!item)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }*/
                    Application_v1* app = (Application_v1*)core->getItem( sessionKey );
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::GENERIC_ERROR);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsSmartDeviceLinkRPCV2::ButtonName btnName;
                    btnName.set((NsSmartDeviceLinkRPCV2::ButtonName::ButtonNameInternal)object->get_buttonName().get());

                    if (core->mButtonsMapping.exist(btnName, app))
                    {
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::IGNORED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        return;
                    }

                    core->mButtonsMapping.addButton( btnName, app );

                    response->set_success(true);
                    response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_UNSUBSCRIBEBUTTON_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An UnsubscribeButton request has been invoked");
                    NsSmartDeviceLinkRPC::UnsubscribeButton_request * object = (NsSmartDeviceLinkRPC::UnsubscribeButton_request*)mobileMsg;
                    NsSmartDeviceLinkRPC::UnsubscribeButton_response* response = new NsSmartDeviceLinkRPC::UnsubscribeButton_response();
                    response->setCorrelationID(object->getCorrelationID());
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN_EXT(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsSmartDeviceLinkRPCV2::ButtonName btnName;
                    btnName.set((NsSmartDeviceLinkRPCV2::ButtonName::ButtonNameInternal)object->get_buttonName().get());

                    if (!core->mButtonsMapping.exist(btnName, app))
                    {
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::IGNORED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        return;
                    }

                    core->mButtonsMapping.removeButton( btnName, app );
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->set_success(true);
                    response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_SHOW_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A Show request has been invoked");
                    LOG4CPLUS_INFO_EXT(mLogger, "message " << mobileMsg->getMethodId() );
                    NsSmartDeviceLinkRPC::Show_request* object = (NsSmartDeviceLinkRPC::Show_request*)mobileMsg;
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPC::Show_response* response = new NsSmartDeviceLinkRPC::Show_response();
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPC::Show_response* response = new NsSmartDeviceLinkRPC::Show_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::Show* showRPC2Request = new NsRPC2Communication::UI::Show();
                    showRPC2Request->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    LOG4CPLUS_INFO_EXT(mLogger, "showrpc2request created");
                    if(object->get_mainField1())
                    {
                        showRPC2Request->set_mainField1(*object->get_mainField1());
                    }
                    LOG4CPLUS_INFO_EXT(mLogger, "setMainField1 was called");
                    if(object->get_mainField2())
                    {
                        showRPC2Request->set_mainField2(*object->get_mainField2());
                    }
                    if(object->get_mediaClock())
                    {
                        showRPC2Request->set_mediaClock(*object->get_mediaClock());
                    }
                    if(object->get_mediaTrack())
                    {
                        showRPC2Request->set_mediaTrack(*object->get_mediaTrack());
                    }
                    if(object->get_statusBar())
                    {
                        showRPC2Request->set_statusBar(*object->get_statusBar());
                    }
                    if(object->get_alignment())
                    {
                        showRPC2Request->set_alignment(
                            static_cast<NsSmartDeviceLinkRPCV2::TextAlignment::TextAlignmentInternal>(object->get_alignment()->get()));
                    }
                    showRPC2Request->set_appId(sessionKey);
                    LOG4CPLUS_INFO_EXT(mLogger, "Show request almost handled" );
                    core->mMessageChaining[showRPC2Request->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    HMIHandler::getInstance().sendRequest(showRPC2Request);
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_SPEAK_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A Speak request has been invoked");
                    NsSmartDeviceLinkRPC::Speak_request* object = (NsSmartDeviceLinkRPC::Speak_request*)mobileMsg;
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPC::Speak_response* response = new NsSmartDeviceLinkRPC::Speak_response();
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL != app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPC::Speak_response* response = new NsSmartDeviceLinkRPC::Speak_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::TTS::Speak* speakRPC2Request = new NsRPC2Communication::TTS::Speak();
                    speakRPC2Request->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());

                    {
                        std::vector<NsSmartDeviceLinkRPC::TTSChunk> chunkOrig = object->get_ttsChunks();
                        std::vector<NsSmartDeviceLinkRPCV2::TTSChunk> chunkRes;
                        for( int i = 0; i < chunkOrig.size(); ++i )
                        {
                            NsSmartDeviceLinkRPCV2::TTSChunk chunk;
                            chunk.set_text(chunkOrig[i].get_text());
                            chunk.set_type(
                                static_cast<NsSmartDeviceLinkRPCV2::SpeechCapabilities::SpeechCapabilitiesInternal>(
                                    chunkOrig[i].get_type().get()));
                            chunkRes.push_back(chunk);
                        }
                        speakRPC2Request->set_ttsChunks(chunkRes);
                    }

                    speakRPC2Request->set_appId(sessionKey);
                    core->mMessageChaining[speakRPC2Request->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    HMIHandler::getInstance().sendRequest(speakRPC2Request);
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_SETGLOBALPROPERTIES_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A SetGlobalProperties request has been invoked");
                    NsSmartDeviceLinkRPC::SetGlobalProperties_request* object = (NsSmartDeviceLinkRPC::SetGlobalProperties_request*)mobileMsg;
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPC::SetGlobalProperties_response* response = new NsSmartDeviceLinkRPC::SetGlobalProperties_response();
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPC::SetGlobalProperties_response* response = new NsSmartDeviceLinkRPC::SetGlobalProperties_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::SetGlobalProperties* setGPRPC2Request = new NsRPC2Communication::UI::SetGlobalProperties();
                    setGPRPC2Request->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[setGPRPC2Request->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    if(object->get_helpPrompt())
                    {
                        std::vector<NsSmartDeviceLinkRPC::TTSChunk> chunkOrig = *object->get_helpPrompt();
                        std::vector<NsSmartDeviceLinkRPCV2::TTSChunk> chunkRes;
                        for( int i = 0; i < chunkOrig.size(); ++i )
                        {
                            NsSmartDeviceLinkRPCV2::TTSChunk chunk;
                            chunk.set_text(chunkOrig[i].get_text());
                            chunk.set_type(
                                static_cast<NsSmartDeviceLinkRPCV2::SpeechCapabilities::SpeechCapabilitiesInternal>(
                                    chunkOrig[i].get_type().get()));
                            chunkRes.push_back(chunk);
                        }
                        setGPRPC2Request->set_helpPrompt(chunkRes);
                    }

                    if(object->get_timeoutPrompt())
                    {
                        std::vector<NsSmartDeviceLinkRPC::TTSChunk> chunkOrig = *object->get_timeoutPrompt();
                        std::vector<NsSmartDeviceLinkRPCV2::TTSChunk> chunkRes;
                        for( int i = 0; i < chunkOrig.size(); ++i )
                        {
                            NsSmartDeviceLinkRPCV2::TTSChunk chunk;
                            chunk.set_text(chunkOrig[i].get_text());
                            chunk.set_type(
                                static_cast<NsSmartDeviceLinkRPCV2::SpeechCapabilities::SpeechCapabilitiesInternal>(
                                    chunkOrig[i].get_type().get()));
                            chunkRes.push_back(chunk);
                        }
                        setGPRPC2Request->set_timeoutPrompt(chunkRes);
                    }

                    setGPRPC2Request->set_appId(sessionKey);
                    HMIHandler::getInstance().sendRequest(setGPRPC2Request);
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_RESETGLOBALPROPERTIES_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A ResetGlobalProperties request has been invoked");
                    NsSmartDeviceLinkRPC::ResetGlobalProperties_request* object = (NsSmartDeviceLinkRPC::ResetGlobalProperties_request*)mobileMsg;
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPC::ResetGlobalProperties_response* response = new NsSmartDeviceLinkRPC::ResetGlobalProperties_response();
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPC::ResetGlobalProperties_response* response = new NsSmartDeviceLinkRPC::ResetGlobalProperties_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::ResetGlobalProperties* resetGPRPC2Request = new NsRPC2Communication::UI::ResetGlobalProperties();
                    resetGPRPC2Request->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[resetGPRPC2Request->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());

                    std::vector<NsSmartDeviceLinkRPCV2::GlobalProperty> propertyResult;
                    convert<NsSmartDeviceLinkRPCV2::GlobalProperty,
                            NsSmartDeviceLinkRPC::GlobalProperty,
                            NsSmartDeviceLinkRPCV2::GlobalProperty::GlobalPropertyInternal>(
                        object->get_properties(),
                        propertyResult);
                    resetGPRPC2Request->set_properties(propertyResult);
                    resetGPRPC2Request->set_appId(sessionKey);
                    HMIHandler::getInstance().sendRequest(resetGPRPC2Request);
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_ALERT_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An Alert request has been invoked");
                    NsSmartDeviceLinkRPC::Alert_request* object = (NsSmartDeviceLinkRPC::Alert_request*)mobileMsg;
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPC::Alert_response* response = new NsSmartDeviceLinkRPC::Alert_response();
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if((NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                        || (NsSmartDeviceLinkRPCV2::HMILevel::HMI_BACKGROUND == app->getApplicationHMIStatusLevel()))
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPC::Alert_response* response = new NsSmartDeviceLinkRPC::Alert_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::Alert* alert = new NsRPC2Communication::UI::Alert();
                    alert->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[alert->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    if(object->get_alertText1())
                    {
                        alert->set_AlertText1(*object->get_alertText1());
                    }
                    if(object->get_alertText2())
                    {
                        alert->set_AlertText2(*object->get_alertText2());
                    }
                    if(object->get_duration())
                    {
                        alert->set_duration(*object->get_duration());
                    }
                    if(object->get_playTone())
                    {
                        alert->set_playTone(*object->get_playTone());
                    }
                    if(object->get_ttsChunks())
                    {
                        const std::vector<NsSmartDeviceLinkRPC::TTSChunk> &oldChunks = *object->get_ttsChunks();
                        std::vector<NsSmartDeviceLinkRPCV2::TTSChunk> chunks(oldChunks.size());
                        for(int i = 0; i < oldChunks.size(); ++i)
                        {
                            NsSmartDeviceLinkRPCV2::TTSChunk chunk;
                            chunk.set_text(oldChunks[i].get_text());
                            chunk.set_type(
                                static_cast<NsSmartDeviceLinkRPCV2::SpeechCapabilities::SpeechCapabilitiesInternal>(
                                    oldChunks[i].get_type().get()));
                            chunks[i] = chunk;
                        }
                        alert->set_ttsChunks(chunks);
                    }
                    alert->set_appId(sessionKey);
                    HMIHandler::getInstance().sendRequest(alert);
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_ADDCOMMAND_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An AddCommand request has been invoked");
                    NsSmartDeviceLinkRPC::AddCommand_request* object = (NsSmartDeviceLinkRPC::AddCommand_request*)mobileMsg;
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPC::AddCommand_response* response = new NsSmartDeviceLinkRPC::AddCommand_response();
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPC::AddCommand_response* response = new NsSmartDeviceLinkRPC::AddCommand_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    const unsigned int& cmdId = object->get_cmdID();

                    MessageChaining * chain = 0;

                    if(object->get_menuParams())
                    {
                        LOG4CPLUS_INFO_EXT(mLogger, " An AddCommand UI request has been invoked");
                        NsRPC2Communication::UI::AddCommand * addCmd = new NsRPC2Communication::UI::AddCommand();
                        addCmd->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                        CommandType cmdType = CommandType::UI;
                        const NsSmartDeviceLinkRPC::MenuParams* menuParams = object->get_menuParams();

                        NsSmartDeviceLinkRPCV2::MenuParams menuParamsRes;
                        menuParamsRes.set_menuName(menuParams->get_menuName());
                        if ( menuParams->get_parentID() )
                        {
                            menuParamsRes.set_parentID(*menuParams->get_parentID());
                        }
                        if ( menuParams->get_position() )
                        {
                            menuParamsRes.set_position(*menuParams->get_position());
                        }

                        addCmd->set_menuParams(menuParamsRes);
                        addCmd->set_cmdId(cmdId);
                        addCmd->set_appId(app->getAppID());
                        if(menuParams->get_parentID())
                        {
                            const unsigned int& menuId = *menuParams->get_parentID();
                            app->addMenuCommand(cmdId, menuId);
                        }

                        chain = core->addChain(chain,
                            sessionKey,
                            object->getCorrelationID());
                        core->mMessageChaining[addCmd->getId()] = chain;
                        CommandParams params;
                        params.menuParams = menuParams;
                        app->addCommand(cmdId, cmdType, params);
                        app->incrementUnrespondedRequestCount(cmdId);
                        HMIHandler::getInstance().sendRequest(addCmd);

                    }
                    if(object->get_vrCommands())
                    {
                        LOG4CPLUS_INFO_EXT(mLogger, " An AddCommand VR request has been invoked");
                        NsRPC2Communication::VR::AddCommand * addCmd = new NsRPC2Communication::VR::AddCommand();
                        addCmd->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                        CommandType cmdType = CommandType::VR;
                        addCmd->set_vrCommands(*object->get_vrCommands());
                        addCmd->set_cmdId(cmdId);
                        addCmd->set_appId(app->getAppID());
                        chain = core->addChain(chain,
                            sessionKey,
                            object->getCorrelationID());
                        core->mMessageChaining[addCmd->getId()] = chain;
                        CommandParams params;
                        params.vrCommands = object->get_vrCommands();
                        app->addCommand(cmdId, cmdType, params);
                        app->incrementUnrespondedRequestCount(cmdId);
                        HMIHandler::getInstance().sendRequest(addCmd);
                    }

                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_DELETECOMMAND_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A DeleteCommand request has been invoked");
                    NsSmartDeviceLinkRPC::DeleteCommand_request* object = (NsSmartDeviceLinkRPC::DeleteCommand_request*)mobileMsg;
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " Application id " << sessionKey
                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPC::DeleteCommand_response* response = new NsSmartDeviceLinkRPC::DeleteCommand_response();
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPC::DeleteCommand_response* response = new NsSmartDeviceLinkRPC::DeleteCommand_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    CommandTypes cmdTypes = app->getCommandTypes(object->get_cmdID());
                    if(cmdTypes.empty())
                    {
                        NsSmartDeviceLinkRPC::DeleteCommand_response* response = new NsSmartDeviceLinkRPC::DeleteCommand_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_DATA);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    const unsigned int& cmdId = object->get_cmdID();
                    MessageChaining * chain = 0;
                    for(CommandTypes::iterator it = cmdTypes.begin(); it != cmdTypes.end(); it++)
                    {
                        CommandType cmdType = *it;
                        if(cmdType == CommandType::UI)
                        {
                            LOG4CPLUS_INFO_EXT(mLogger, " A DeleteCommand UI request has been invoked");
                            NsRPC2Communication::UI::DeleteCommand* deleteCmd = new NsRPC2Communication::UI::DeleteCommand();
                            deleteCmd->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                            deleteCmd->set_appId(app->getAppID());
                            deleteCmd->set_cmdId(cmdId);
                            app->removeCommand(cmdId, cmdType);
                            app->incrementUnrespondedRequestCount(cmdId);
                            app->removeMenuCommand(cmdId);
                            chain = core->addChain(chain,
                                sessionKey,
                                object->getCorrelationID());
                            core->mMessageChaining[deleteCmd->getId()] = chain;
                            HMIHandler::getInstance().sendRequest(deleteCmd);
                        }
                        else if(cmdType == CommandType::VR)
                        {
                            LOG4CPLUS_INFO_EXT(mLogger, " A DeleteCommand VR request has been invoked");
                            NsRPC2Communication::VR::DeleteCommand* deleteCmd = new NsRPC2Communication::VR::DeleteCommand();
                            deleteCmd->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                            deleteCmd->set_cmdId(cmdId);
                            deleteCmd->set_appId(app->getAppID());
                            app->removeCommand(cmdId, cmdType);
                            app->incrementUnrespondedRequestCount(cmdId);
                            chain = core->addChain(chain,
                                sessionKey,
                                object->getCorrelationID());
                            core->mMessageChaining[deleteCmd->getId()] = chain;
                            HMIHandler::getInstance().sendRequest(deleteCmd);
                        }
                    }
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_ADDSUBMENU_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An AddSubmenu request has been invoked");
                    NsSmartDeviceLinkRPC::AddSubMenu_request* object = (NsSmartDeviceLinkRPC::AddSubMenu_request*)mobileMsg;
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPC::AddSubMenu_response* response = new NsSmartDeviceLinkRPC::AddSubMenu_response();
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPC::AddSubMenu_response* response = new NsSmartDeviceLinkRPC::AddSubMenu_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    const MenuValue* menu = app->findMenu(object->get_menuID());
                    if (menu)
                    {
                        NsSmartDeviceLinkRPC::AddSubMenu_response* response = new NsSmartDeviceLinkRPC::AddSubMenu_response;
                        response->set_success(false);
                        response->setCorrelationID(object->getCorrelationID());

                        if (menu->first == object->get_menuName())
                        {
                            response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::DUPLICATE_NAME);
                            MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                            return;
                        }

                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_ID);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        return;
                    }

                    NsRPC2Communication::UI::AddSubMenu* addSubMenu = new NsRPC2Communication::UI::AddSubMenu();
                    addSubMenu->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[addSubMenu->getId()] = new MessageChaining(
                                sessionKey,
                                object->getCorrelationID());
                    addSubMenu->set_menuId(object->get_menuID());
                    addSubMenu->set_menuName(object->get_menuName());
                    if(object->get_position())
                    {
                        addSubMenu->set_position(*object->get_position());
                    }
                    addSubMenu->set_appId(app->getAppID());
                    app->addMenu(object->get_menuID(), object->get_menuName(), object->get_position());
                    HMIHandler::getInstance().sendRequest(addSubMenu);
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_DELETESUBMENU_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A DeleteSubmenu request has been invoked");
                    NsSmartDeviceLinkRPC::DeleteSubMenu_request* object = (NsSmartDeviceLinkRPC::DeleteSubMenu_request*)mobileMsg;
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPC::DeleteSubMenu_response* response = new NsSmartDeviceLinkRPC::DeleteSubMenu_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPC::DeleteSubMenu_response* response = new NsSmartDeviceLinkRPC::DeleteSubMenu_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    const unsigned int& menuId = object->get_menuID();
                    if(!app->findMenu(menuId))
                    {
                        LOG4CPLUS_WARN(mLogger, " menuId " << menuId
                                            << " hasn't been associated with the application " << app->getName() << " id " << app->getAppID() << " !");
                        NsSmartDeviceLinkRPC::DeleteSubMenu_response* response = new NsSmartDeviceLinkRPC::DeleteSubMenu_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_ID);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::DeleteSubMenu* delSubMenu = new NsRPC2Communication::UI::DeleteSubMenu();
                    delSubMenu->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[delSubMenu->getId()] = new MessageChaining(
                                sessionKey,
                                object->getCorrelationID());
                    delSubMenu->set_menuId(menuId);
                    delSubMenu->set_appId(app->getAppID());
                    HMIHandler::getInstance().sendRequest(delSubMenu);
                    core->menuMapping[delSubMenu->getId()] = menuId;
                    // app->removeMenu(menuId);
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_CREATEINTERACTIONCHOICESET_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A CreateInteractionChoiceSet request has been invoked");
                    NsSmartDeviceLinkRPC::CreateInteractionChoiceSet_request* object = (NsSmartDeviceLinkRPC::CreateInteractionChoiceSet_request*)mobileMsg;
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPC::CreateInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPC::CreateInteractionChoiceSet_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPC::CreateInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPC::CreateInteractionChoiceSet_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::CreateInteractionChoiceSet* createInteractionChoiceSet = new NsRPC2Communication::UI::CreateInteractionChoiceSet();
                    createInteractionChoiceSet->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[createInteractionChoiceSet->getId()] = new MessageChaining(
                                sessionKey,
                                object->getCorrelationID());
                    std::vector<NsSmartDeviceLinkRPC::Choice> chSetOrig = object->get_choiceSet();
                    std::vector<NsSmartDeviceLinkRPCV2::Choice> chSetRes;
                    for(int i = 0; i < chSetOrig.size(); ++i)
                    {
                        NsSmartDeviceLinkRPCV2::Choice choice;
                        choice.set_choiceID(chSetOrig[i].get_choiceID());
                        choice.set_menuName(chSetOrig[i].get_menuName());
                        choice.set_vrCommands(chSetOrig[i].get_vrCommands());
                        chSetRes.push_back(choice);
                    }
                    createInteractionChoiceSet->set_choiceSet(chSetRes);
                    createInteractionChoiceSet->set_interactionChoiceSetID(object->get_interactionChoiceSetID());
                    createInteractionChoiceSet->set_appId(app->getAppID());
                    app->addChoiceSet(object->get_interactionChoiceSetID(), object->get_choiceSet());
                    HMIHandler::getInstance().sendRequest(createInteractionChoiceSet);
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_DELETEINTERACTIONCHOICESET_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A DeleteInteractionChoiceSet request has been invoked");
                    NsSmartDeviceLinkRPC::DeleteInteractionChoiceSet_request* object = (NsSmartDeviceLinkRPC::DeleteInteractionChoiceSet_request*)mobileMsg;

                    if (core->mPerformInteractionFlag == object->get_interactionChoiceSetID())
                    {
                        NsSmartDeviceLinkRPC::DeleteInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPC::DeleteInteractionChoiceSet_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::IN_USE);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        return;
                    }

                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPC::DeleteInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPC::DeleteInteractionChoiceSet_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPC::DeleteInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPC::DeleteInteractionChoiceSet_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    const unsigned int& choiceSetId = object->get_interactionChoiceSetID();
                    const ChoiceSetV1* choiceSetFound = app->findChoiceSet(choiceSetId);
                    if(!choiceSetFound)
                    {
                        LOG4CPLUS_WARN(mLogger, " a choice set " << choiceSetId
                                            << " hasn't been registered within the application " << app->getName() << " id" << app->getAppID() << " !");
                        NsSmartDeviceLinkRPC::DeleteInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPC::DeleteInteractionChoiceSet_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_ID);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        return;
                    }

                    NsRPC2Communication::UI::DeleteInteractionChoiceSet* deleteInteractionChoiceSet = new NsRPC2Communication::UI::DeleteInteractionChoiceSet();
                    deleteInteractionChoiceSet->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[deleteInteractionChoiceSet->getId()] = new MessageChaining(
                                sessionKey,
                                object->getCorrelationID());
                    deleteInteractionChoiceSet->set_interactionChoiceSetID(object->get_interactionChoiceSetID());
                    deleteInteractionChoiceSet->set_appId(app->getAppID());
                    app->removeChoiceSet(object->get_interactionChoiceSetID());
                    HMIHandler::getInstance().sendRequest(deleteInteractionChoiceSet);
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_PERFORMINTERACTION_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A PerformInteraction request has been invoked");
                    NsSmartDeviceLinkRPC::PerformInteraction_request* object = (NsSmartDeviceLinkRPC::PerformInteraction_request*)mobileMsg;
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPC::PerformInteraction_response* response = new NsSmartDeviceLinkRPC::PerformInteraction_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL != app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName()
                            << " with session key "
                            << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPC::PerformInteraction_response* response = new NsSmartDeviceLinkRPC::PerformInteraction_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    const std::vector<unsigned int>& choiceSets = object->get_interactionChoiceSetIDList();
                    for(std::vector<unsigned int>::const_iterator it = choiceSets.begin(); it != choiceSets.end(); it++)
                    {
                        const unsigned int& choiceSetId = *it;
                        const ChoiceSetV1* choiceSetFound = app->findChoiceSet(choiceSetId);
                        if(!choiceSetFound)
                        {
                            LOG4CPLUS_WARN(mLogger, " a choice set " << choiceSetId
                                                << " hasn't been registered within the application " << app->getName()
                                                << " id" << app->getAppID() << " !");
                            NsSmartDeviceLinkRPC::PerformInteraction_response* response = new NsSmartDeviceLinkRPC::PerformInteraction_response;
                            response->set_success(false);
                            response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_DATA);
                            response->setCorrelationID(object->getCorrelationID());
                            MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                            return;
                        }
                    }
                    NsRPC2Communication::UI::PerformInteraction* performInteraction = new NsRPC2Communication::UI::PerformInteraction();
                    performInteraction->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    performInteraction->set_appId(sessionKey);
                    performInteraction->set_interactionChoiceSetIDList(choiceSets);
                    core->mMessageChaining[performInteraction->getId()] = new MessageChaining(
                                sessionKey,
                                object->getCorrelationID());
                    if(object->get_helpPrompt())
                    {
                        std::vector<NsSmartDeviceLinkRPC::TTSChunk> chunkOrig = *object->get_helpPrompt();
                        std::vector<NsSmartDeviceLinkRPCV2::TTSChunk> chunkRes;
                        for( int i = 0; i < chunkOrig.size(); ++i )
                        {
                            NsSmartDeviceLinkRPCV2::TTSChunk chunk;
                            chunk.set_text(chunkOrig[i].get_text());
                            chunk.set_type(
                                static_cast<NsSmartDeviceLinkRPCV2::SpeechCapabilities::SpeechCapabilitiesInternal>(
                                    chunkOrig[i].get_type().get()));
                            chunkRes.push_back(chunk);
                        }
                        performInteraction->set_helpPrompt(chunkRes);
                    }

                    {
                        std::vector<NsSmartDeviceLinkRPC::TTSChunk> chunkOrig = object->get_initialPrompt();
                        std::vector<NsSmartDeviceLinkRPCV2::TTSChunk> chunkRes;
                        for( int i = 0; i < chunkOrig.size(); ++i )
                        {
                            NsSmartDeviceLinkRPCV2::TTSChunk chunk;
                            chunk.set_text(chunkOrig[i].get_text());
                            chunk.set_type(
                                static_cast<NsSmartDeviceLinkRPCV2::SpeechCapabilities::SpeechCapabilitiesInternal>(
                                    chunkOrig[i].get_type().get()));
                            chunkRes.push_back(chunk);
                        }
                        performInteraction->set_initialPrompt(chunkRes);
                    }

                    performInteraction->set_initialText(object->get_initialText());
                    performInteraction->set_interactionMode(
                        static_cast<NsSmartDeviceLinkRPCV2::InteractionMode::InteractionModeInternal>(
                            object->get_interactionMode().get()));
                    if(object->get_timeout())
                    {
                        performInteraction->set_timeout(*object->get_timeout());
                    }
                    if(object->get_timeoutPrompt())
                    {
                        std::vector<NsSmartDeviceLinkRPC::TTSChunk> chunkOrig = *object->get_timeoutPrompt();
                        std::vector<NsSmartDeviceLinkRPCV2::TTSChunk> chunkRes;
                        for( int i = 0; i < chunkOrig.size(); ++i )
                        {
                            NsSmartDeviceLinkRPCV2::TTSChunk chunk;
                            chunk.set_text(chunkOrig[i].get_text());
                            chunk.set_type(
                                static_cast<NsSmartDeviceLinkRPCV2::SpeechCapabilities::SpeechCapabilitiesInternal>(
                                    chunkOrig[i].get_type().get()));
                            chunkRes.push_back(chunk);
                        }
                        performInteraction->set_timeoutPrompt(chunkRes);
                    }
                    HMIHandler::getInstance().sendRequest(performInteraction);
                    core->mPerformInteractionFlag = choiceSets[0];
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_SETMEDIACLOCKTIMER_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A SetMediaClockTimer request has been invoked");
                    NsSmartDeviceLinkRPC::SetMediaClockTimer_request* object = (NsSmartDeviceLinkRPC::SetMediaClockTimer_request*)mobileMsg;
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPC::SetMediaClockTimer_response* response = new NsSmartDeviceLinkRPC::SetMediaClockTimer_response();
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel()
                        || !app->getIsMediaApplication() )
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPC::SetMediaClockTimer_response* response = new NsSmartDeviceLinkRPC::SetMediaClockTimer_response;
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::SetMediaClockTimer* setTimer = new NsRPC2Communication::UI::SetMediaClockTimer();
                    setTimer->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    setTimer->set_appId(sessionKey);

                    if(object->get_startTime())
                    {
                        NsSmartDeviceLinkRPCV2::StartTime to;
                        to.set_hours(object->get_startTime()->get_hours());
                        to.set_minutes(object->get_startTime()->get_minutes());
                        to.set_seconds(object->get_startTime()->get_seconds());
                        setTimer->set_startTime(to);
                    }
                    const NsSmartDeviceLinkRPC::UpdateMode& updateMode = object->get_updateMode();
                    NsSmartDeviceLinkRPCV2::UpdateMode updateModeV2;
                    updateModeV2.set((NsSmartDeviceLinkRPCV2::UpdateMode::UpdateModeInternal)updateMode.get());
                    setTimer->set_updateMode(updateModeV2);
                    HMIHandler::getInstance().sendRequest(setTimer);

                    core->mMessageChaining[setTimer->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());

                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_ENCODEDSYNCPDATA_REQUEST:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An EncodedSyncPData request has been invoked");
                    NsSmartDeviceLinkRPC::EncodedSyncPData_request* object = (NsSmartDeviceLinkRPC::EncodedSyncPData_request*)mobileMsg;
                    NsSmartDeviceLinkRPC::EncodedSyncPData_response* response = new NsSmartDeviceLinkRPC::EncodedSyncPData_response;
                    response->setCorrelationID(object->getCorrelationID());
                    Application_v1* app = (Application_v1*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    if(object->get_data())
                    {
                        Application* app = core->getItem(sessionKey);
                        const std::string& name = app->getName();
                        core->mSyncPManager.setPData(*object->get_data(), name, object->getMethodId());
                        response->set_success(true);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                    }
                    else
                    {
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_DATA);
                    }

                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_SHOW_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_SPEAK_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_SETGLOBALPROPERTIES_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_RESETGLOBALPROPERTIES_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_REGISTERAPPINTERFACE_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_SUBSCRIBEBUTTON_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_UNSUBSCRIBEBUTTON_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_ONAPPINTERFACEUNREGISTERED:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_ALERT_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_ADDCOMMAND_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_ADDSUBMENU_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_CREATEINTERACTIONCHOICESET_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_DELETECOMMAND_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_DELETEINTERACTIONCHOICESET_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_DELETESUBMENU_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_ENCODEDSYNCPDATA_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_GENERICRESPONSE_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_PERFORMINTERACTION_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_SETMEDIACLOCKTIMER_RESPONSE:
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_UNREGISTERAPPINTERFACE_RESPONSE:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A " << mobileMsg->getMethodId() << " response or notification has been invoked");
                    MobileHandler::getInstance().sendRPCMessage(mobileMsg, sessionKey);
                    break;
                }
                case NsSmartDeviceLinkRPC::Marshaller::METHOD_INVALID:
                default:
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, " An undefined or invalid RPC message " << mobileMsg->getMethodId() << " has been received!");
                    NsSmartDeviceLinkRPC::GenericResponse_response* response = new NsSmartDeviceLinkRPC::GenericResponse_response();
                    response->set_success(false);
                    response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_DATA);
                    response->setCorrelationID(mobileMsg->getCorrelationID());
                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                    break;
                }
            }
        }
        else if ( 2 == mobileMsg->getProtocolVersion() )
        {
            LOG4CPLUS_INFO_EXT(mLogger,"Received message of version 2.");
            switch(mobileMsg->getMethodId())
            {
                case NsSmartDeviceLinkRPCV2::FunctionID::RegisterAppInterfaceID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, "Message id is NsSmartDeviceLinkRPCV2::FunctionID::RegisterAppInterfaceID");
                    NsSmartDeviceLinkRPCV2::RegisterAppInterface_request * object = (NsSmartDeviceLinkRPCV2::RegisterAppInterface_request*)mobileMsg;
                    NsSmartDeviceLinkRPCV2::RegisterAppInterface_response* response = new NsSmartDeviceLinkRPCV2::RegisterAppInterface_response();
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::RegisterAppInterfaceID);
                    response->set_success(true);
                    response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                    response->setCorrelationID(object->getCorrelationID());

                    const std::string& appName = object->get_appName();

                    if(core->getItem(sessionKey))
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " Application " << appName << " is already registered!");
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_REGISTERED_ALREADY);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    Application_v2* app = (Application_v2*)core->registerApplication( object, sessionKey );

                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " Application " << appName << " hasn't been registered!");
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    response->set_buttonCapabilities(core->mButtonCapabilitiesV2.get());
                    response->set_displayCapabilities(core->mDisplayCapabilitiesV2);
                    response->set_hmiZoneCapabilities(core->mHmiZoneCapabilitiesV2.get());

                    response->set_hmiDisplayLanguage(core->mUiLanguageV2);
                    response->set_language(core->mVrLanguageV2);
                    if ( object->get_languageDesired().get() != core->mVrLanguageV2.get()
                            || object->get_hmiDisplayLanguageDesired().get() != core->mUiLanguageV2.get())
                    {
                        LOG4CPLUS_WARN(mLogger, "Wrong language on registering application " << appName);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::WRONG_LANGUAGE);
                    }
                    response->set_speechCapabilities(core->mSpeechCapabilitiesV2.get());
                    response->set_vrCapabilities(core->mVrCapabilitiesV2.get());
                    response->set_syncMsgVersion(app->getSyncMsgVersion());
                    response->set_softButtonCapabilities(core->mSoftButtonCapabilities.get());
                    response->set_presetBankCapabilities(core->mPresetBankCapabilities);
                    response->set_vehicleType(core->mVehicleType);

                    LOG4CPLUS_INFO_EXT(mLogger, " A RegisterAppInterface response for the app "  << app->getName() << " gets sent to a mobile side... ");
                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);

                    NsRPC2Communication::BasicCommunication::OnAppRegistered* appRegistered = new NsRPC2Communication::BasicCommunication::OnAppRegistered();
                    NsSmartDeviceLinkRPCV2::HMIApplication hmiApp;
                    hmiApp.set_appName(app->getName());
                    hmiApp.set_appId(app->getAppID());
                    hmiApp.set_isMediaApplication(app->getIsMediaApplication());

                    std::map<int,DeviceStorage>::const_iterator it = core->mDevices.find( app->getDeviceHandle() );
                    std::string currentDeviceName = "";
                    if ( core->mDevices.end() != it )
                    {
                        currentDeviceName = it->second.getUserFriendlyName();
                    }

                    hmiApp.set_deviceName(currentDeviceName);
                    hmiApp.set_hmiDisplayLanguageDesired(app->getHMIDisplayLanguageDesired());
                    hmiApp.set_languageDesired(app->getLanguageDesired());

                    if (!app->getAppType().empty())
                    {
                        hmiApp.set_appType(app->getAppType());
                    }

                    if (!app->getTtsName().empty())
                    {
                        hmiApp.set_ttsName(app->getTtsName());
                    }
                    appRegistered->set_application(hmiApp);

                    HMIHandler::getInstance().sendNotification(appRegistered);

                    NsSmartDeviceLinkRPCV2::OnHMIStatus* status = new NsSmartDeviceLinkRPCV2::OnHMIStatus();
                    status->set_hmiLevel(app->getApplicationHMIStatusLevel());
                    status->set_audioStreamingState(app->getApplicationAudioStreamingState());
                    status->set_systemContext(app->getSystemContext());
                    status->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnHMIStatusID);
                    status->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                    MobileHandler::getInstance().sendRPCMessage(status, sessionKey);

                    LOG4CPLUS_INFO_EXT(mLogger, " A RegisterAppInterface request was successful: registered an app " << app->getName());
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::UnregisterAppInterfaceID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An UnregisterAppInterface request has been invoked");
                    NsSmartDeviceLinkRPCV2::UnregisterAppInterface_request * object = (NsSmartDeviceLinkRPCV2::UnregisterAppInterface_request*)mobileMsg;
                    Application* app = core->getItem(sessionKey);
                    NsSmartDeviceLinkRPCV2::UnregisterAppInterface_response* response = new NsSmartDeviceLinkRPCV2::UnregisterAppInterface_response();
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::UnregisterAppInterfaceID);
                    response->setCorrelationID(object->getCorrelationID());

                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    response->set_success(true);
                    response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);

                    /*NsSmartDeviceLinkRPCV2::OnAppInterfaceUnregistered* msgUnregistered = new NsSmartDeviceLinkRPCV2::OnAppInterfaceUnregistered();
                    msgUnregistered->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                    msgUnregistered->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnAppInterfaceUnregisteredID);
                    msgUnregistered->set_reason(NsSmartDeviceLinkRPCV2::AppInterfaceUnregisteredReason(NsSmartDeviceLinkRPCV2::AppInterfaceUnregisteredReason::USER_EXIT));
                    MobileHandler::getInstance().sendRPCMessage(msgUnregistered, sessionKey);

                    std::string appName = app->getName();
                    // core->removeAppFromHmi(app, sessionKey);

                    NsRPC2Communication::BasicCommunication::OnAppUnregistered* appUnregistered = new NsRPC2Communication::BasicCommunication::OnAppUnregistered();
                    appUnregistered->set_appName(appName);
                    appUnregistered->set_appId(app->getAppID());
                    appUnregistered->set_reason(NsSmartDeviceLinkRPCV2::AppInterfaceUnregisteredReason::USER_EXIT);
                    HMIHandler::getInstance().sendNotification(appUnregistered);*/
                    LOG4CPLUS_INFO_EXT(mLogger, " An application " << app->getName() << " has been unregistered successfully ");
                    core->unregisterApplication( sessionKey );
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::SubscribeButtonID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A SubscribeButton request has been invoked");
                    NsSmartDeviceLinkRPCV2::SubscribeButton_request * object = (NsSmartDeviceLinkRPCV2::SubscribeButton_request*)mobileMsg;
                    NsSmartDeviceLinkRPCV2::SubscribeButton_response* response = new NsSmartDeviceLinkRPCV2::SubscribeButton_response();
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SubscribeButtonID);
                    response->setCorrelationID(object->getCorrelationID());

                    /*RegistryItem* item = AppMgrRegistry::getInstance().getItem(sessionKey);
                    if(!item)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }*/
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::GENERIC_ERROR);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    if (core->mButtonsMapping.exist(object->get_buttonName(), app))
                    {
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::IGNORED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        return;
                    }

                    core->mButtonsMapping.addButton( object->get_buttonName(), app );
                    response->set_success(true);
                    response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::UnsubscribeButtonID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An UnsubscribeButton request has been invoked");
                    NsSmartDeviceLinkRPCV2::UnsubscribeButton_request * object = (NsSmartDeviceLinkRPCV2::UnsubscribeButton_request*)mobileMsg;
                    NsSmartDeviceLinkRPCV2::UnsubscribeButton_response* response = new NsSmartDeviceLinkRPCV2::UnsubscribeButton_response();
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::UnsubscribeButtonID);
                    response->setCorrelationID(object->getCorrelationID());

                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    if (!core->mButtonsMapping.exist(object->get_buttonName(), app))
                    {
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::IGNORED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        return;
                    }

                    core->mButtonsMapping.removeButton( object->get_buttonName(), app );
                    response->set_success(true);
                    response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::SetMediaClockTimerID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A SetMediaClockTimer request has been invoked");
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPCV2::SetMediaClockTimer_response* response = new NsSmartDeviceLinkRPCV2::SetMediaClockTimer_response();
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SetMediaClockTimerID);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(mobileMsg->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel()
                        || !app->getIsMediaApplication())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key "
                            << sessionKey << " has not been activated yet or not media!" );
                        NsSmartDeviceLinkRPCV2::SetMediaClockTimer_response* response = new NsSmartDeviceLinkRPCV2::SetMediaClockTimer_response;
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SetMediaClockTimerID);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(mobileMsg->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::SetMediaClockTimer* setTimer = new NsRPC2Communication::UI::SetMediaClockTimer();
                    setTimer->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    setTimer->set_appId(sessionKey);

                    NsSmartDeviceLinkRPCV2::SetMediaClockTimer_request* object = (NsSmartDeviceLinkRPCV2::SetMediaClockTimer_request*)mobileMsg;
                    if(object->get_startTime())
                    {
                        setTimer->set_startTime(*object->get_startTime());
                    }
                    core->mMessageChaining[setTimer->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    setTimer->set_updateMode(object->get_updateMode());
                    HMIHandler::getInstance().sendRequest(setTimer);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::PutFileID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An PutFile request has been invoked");
                    NsSmartDeviceLinkRPCV2::PutFile_request* object = (NsSmartDeviceLinkRPCV2::PutFile_request*)mobileMsg;
                    NsSmartDeviceLinkRPCV2::PutFile_response* response = new NsSmartDeviceLinkRPCV2::PutFile_response;
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::PutFileID);
                    response->setCorrelationID(object->getCorrelationID());
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    unsigned long int freeSpace = WorkWithOS::getAvailableSpace();
                    const std::string& syncFileName = object->get_syncFileName();
                    const NsSmartDeviceLinkRPCV2::FileType& fileType = object->get_fileType();
                    bool persistentFile = object->get_persistentFile();
                    const std::vector<unsigned char>* fileData = object->getBinaryData();

                    bool isSyncFileName = !syncFileName.empty();
                    bool isFileData = fileData && !fileData->empty();
                    if (isSyncFileName && isFileData)
                    {
                        LOG4CPLUS_INFO_EXT(mLogger, "Trying to save file  of size " << fileData->size());
                        bool flag = false;
                        if (freeSpace > fileData->size())
                        {
                            std::string relativeFilePath = WorkWithOS::createDirectory(app->getName());

                            relativeFilePath += "/";
                            relativeFilePath += syncFileName;

                            LOG4CPLUS_INFO(mLogger, "Relative path to file " << relativeFilePath);

                            std::string fullFilePath = WorkWithOS::getFullPath( relativeFilePath );

                            LOG4CPLUS_INFO(mLogger, "Full path to file " << fullFilePath);

                            if (WorkWithOS::checkIfFileExists(fullFilePath))
                            {
                                WorkWithOS::deleteFile(fullFilePath);
                            }

                            LOG4CPLUS_INFO_EXT(mLogger, "Saving to file " << fullFilePath);
                            flag = WorkWithOS::createFileAndWrite(fullFilePath, *fileData);
                            if (persistentFile)
                            {
                                app->addPersistentFile(syncFileName);
                            }
                        }

                        if (flag)
                        {
                            response->set_success(true);
                            response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                            response->set_spaceAvailable(freeSpace);
                        }
                        else
                        {
                            response->set_success(false);
                            response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::GENERIC_ERROR);
                            response->set_spaceAvailable(freeSpace);
                        }
                    }
                    else
                    {
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_DATA);
                        response->set_spaceAvailable(freeSpace);
                    }

                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::DeleteFileID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An DeleteFile request has been invoked");
                    NsSmartDeviceLinkRPCV2::DeleteFile_request* object = (NsSmartDeviceLinkRPCV2::DeleteFile_request*)mobileMsg;
                    NsSmartDeviceLinkRPCV2::DeleteFile_response* response = new NsSmartDeviceLinkRPCV2::DeleteFile_response;
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteFileID);
                    response->setCorrelationID(object->getCorrelationID());
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    unsigned long int freeSpace = WorkWithOS::getAvailableSpace();
                    const std::string& syncFileName = object->get_syncFileName();
                    if(!syncFileName.empty())
                    {
                        std::string relativeFilePath = app->getName();
                        relativeFilePath += "/";
                        relativeFilePath += syncFileName;

                        std::string fullFilePath = WorkWithOS::getFullPath( relativeFilePath );

                        LOG4CPLUS_INFO(mLogger, "Trying to remove file " << fullFilePath);

                        if ( WorkWithOS::deleteFile(fullFilePath) )
                        {
                            response->set_success(true);
                            response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                            response->set_spaceAvailable(freeSpace);
                            app->removePersistentFile(syncFileName);
                        }
                        else
                        {
                            response->set_success(false);
                            response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::GENERIC_ERROR);
                            response->set_spaceAvailable(freeSpace);
                        }
                    }
                    else
                    {
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_DATA);
                        response->set_spaceAvailable(freeSpace);
                    }

                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::ListFilesID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An ListFiles request has been invoked");
                    NsSmartDeviceLinkRPCV2::ListFiles_request* object = (NsSmartDeviceLinkRPCV2::ListFiles_request*)mobileMsg;
                    NsSmartDeviceLinkRPCV2::ListFiles_response* response = new NsSmartDeviceLinkRPCV2::ListFiles_response;
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ListFilesID);
                    response->setCorrelationID(object->getCorrelationID());
                    unsigned long int freeSpace = WorkWithOS::getAvailableSpace();

                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    bool successFlag = false;

                    if ( WorkWithOS::checkIfDirectoryExists(app->getName()))
                    {
                        const std::string & fullDirectoryPath = WorkWithOS::getFullPath(app->getName());
                        std::vector<std::string> listFiles = WorkWithOS::listFilesInDirectory( fullDirectoryPath );
                        if (!listFiles.empty())
                        {
                            successFlag = true;
                            response->set_filenames(listFiles);
                            response->set_success(true);
                            response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                            response->set_spaceAvailable(freeSpace);
                        }
                    }

                    if ( !successFlag )
                    {
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::GENERIC_ERROR);
                        response->set_spaceAvailable(freeSpace);
                    }

                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::SliderID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A Slider request has been invoked");
                    NsSmartDeviceLinkRPCV2::Slider_request* request = (NsSmartDeviceLinkRPCV2::Slider_request*)mobileMsg;
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPCV2::Slider_response* response = new NsSmartDeviceLinkRPCV2::Slider_response;
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SliderID);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(request->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL != app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::Slider_response* response = new NsSmartDeviceLinkRPCV2::Slider_response;
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SliderID);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(request->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::Slider* slider = new NsRPC2Communication::UI::Slider();

                    slider->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[slider->getId()] = new MessageChaining(
                        sessionKey,
                        request->getCorrelationID());

                    slider->set_numTicks(request->get_numTicks());

                    slider->set_sliderHeader(request->get_sliderHeader());

                    if (request->get_sliderFooter())
                    {
                        slider->set_sliderFooter(*(request->get_sliderFooter()));
                    }

                    slider->set_position(request->get_position());
                    slider->set_timeout(request->get_timeout());
                    slider->set_appId(sessionKey);
                    HMIHandler::getInstance().sendRequest(slider);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::SetAppIconID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A SetAppIcon request has been invoked");
                    NsSmartDeviceLinkRPCV2::SetAppIcon_request* request = static_cast<NsSmartDeviceLinkRPCV2::SetAppIcon_request*>(mobileMsg);
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPCV2::SetAppIcon_response* response = new NsSmartDeviceLinkRPCV2::SetAppIcon_response;
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SetAppIconID);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(request->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::SetAppIcon_response* response = new NsSmartDeviceLinkRPCV2::SetAppIcon_response;
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SetAppIconID);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(request->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    NsRPC2Communication::UI::SetAppIcon* setAppIcon = new NsRPC2Communication::UI::SetAppIcon();
                    setAppIcon->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());

                    char currentAppPath[FILENAME_MAX];
                    char fullPathToSyncFileName[FILENAME_MAX];

                    memset(currentAppPath, 0, FILENAME_MAX);
                    memset(fullPathToSyncFileName, 0, FILENAME_MAX);

                    getcwd(currentAppPath, FILENAME_MAX);
                    const std::string& syncFileName = request->get_syncFileName();
                    // TODO(AK): We look for icon in current app dir.
                    // TODO(PV): and that is correct, why todo?
                    snprintf(fullPathToSyncFileName, FILENAME_MAX - 1, "%s/%s/%s"
                        , currentAppPath, app->getName().c_str(), syncFileName.c_str());

                    LOG4CPLUS_INFO_EXT(mLogger, "Full path to sync file name: " << fullPathToSyncFileName);

                    setAppIcon->set_syncFileName(fullPathToSyncFileName);
                    setAppIcon->set_appId(app->getAppID());

                    core->mMessageChaining[setAppIcon->getId()] = new MessageChaining(
                        sessionKey,
                        request->getCorrelationID());
                    HMIHandler::getInstance().sendRequest(setAppIcon);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::ScrollableMessageID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A ScrollableMessageID request has been invoked");
                    NsSmartDeviceLinkRPCV2::ScrollableMessage_request* request = static_cast<NsSmartDeviceLinkRPCV2::ScrollableMessage_request*>(mobileMsg);
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPCV2::ScrollableMessage_response* response = new NsSmartDeviceLinkRPCV2::ScrollableMessage_response;
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ScrollableMessageID);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(request->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL != app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::ScrollableMessage_response* response = new NsSmartDeviceLinkRPCV2::ScrollableMessage_response;
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ScrollableMessageID);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(request->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    NsRPC2Communication::UI::ScrollableMessage* scrollableMessage = new NsRPC2Communication::UI::ScrollableMessage();
                    if (!scrollableMessage)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "Can't create scrollable message object.");
                        return;
                    }
                    scrollableMessage->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    scrollableMessage->set_appId(app->getAppID());
                    scrollableMessage->set_scrollableMessageBody(request->get_scrollableMessageBody());
                    if (request->get_timeout())
                    {
                        scrollableMessage->set_timeout(*(request->get_timeout()));
                    }
                    if ( request->get_softButtons() )
                    {
                        scrollableMessage->set_softButtons(*(request->get_softButtons()));
                    }
                    core->mMessageChaining[scrollableMessage->getId()] = new MessageChaining(
                        sessionKey,
                        request->getCorrelationID());
                    HMIHandler::getInstance().sendRequest(scrollableMessage);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::EncodedSyncPDataID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An EncodedSyncPData request has been invoked");
                    NsSmartDeviceLinkRPCV2::EncodedSyncPData_request* object = (NsSmartDeviceLinkRPCV2::EncodedSyncPData_request*)mobileMsg;
                    NsSmartDeviceLinkRPCV2::EncodedSyncPData_response* response = new NsSmartDeviceLinkRPCV2::EncodedSyncPData_response;
                    response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::EncodedSyncPDataID);
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->setCorrelationID(object->getCorrelationID());
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        response->set_success(false);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::EncodedSyncPDataID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        response->set_success(false);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::EncodedSyncPDataID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    const std::string& name = app->getName();
                    core->mSyncPManager.setPData(object->get_data(), name, object->getMethodId());
                    response->set_success(true);
                    response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);

                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::SetGlobalPropertiesID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A SetGlobalProperties request has been invoked");
                    NsSmartDeviceLinkRPCV2::SetGlobalProperties_request* object = (NsSmartDeviceLinkRPCV2::SetGlobalProperties_request*)mobileMsg;
                    NsSmartDeviceLinkRPCV2::SetGlobalProperties_response * mobileResponse = new NsSmartDeviceLinkRPCV2::SetGlobalProperties_response;
                    mobileResponse->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SetGlobalPropertiesID);
                    mobileResponse->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    mobileResponse->setCorrelationID(object->getCorrelationID());
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        mobileResponse->set_success(false);
                        mobileResponse->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(mobileResponse, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        mobileResponse->set_success(false);
                        mobileResponse->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        MobileHandler::getInstance().sendRPCMessage(mobileResponse, sessionKey);
                        break;
                    }

                    if ((object->get_vrHelp() && object->get_vrHelpTitle()) ||
                        (!object->get_vrHelp() && !object->get_vrHelpTitle()))
                    {
                        NsRPC2Communication::UI::SetGlobalProperties* setGPRPC2Request = new NsRPC2Communication::UI::SetGlobalProperties();
                        setGPRPC2Request->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                        core->mMessageChaining[setGPRPC2Request->getId()] = new MessageChaining(
                            sessionKey,
                            object->getCorrelationID());
                        if(object->get_helpPrompt())
                        {
                            setGPRPC2Request->set_helpPrompt(*object->get_helpPrompt());
                        }

                        if(object->get_timeoutPrompt())
                        {
                            setGPRPC2Request->set_timeoutPrompt(*object->get_timeoutPrompt());
                        }

                        if(object->get_vrHelp())
                        {
                            setGPRPC2Request->set_vrHelp(*object->get_vrHelp());
                        }
                        if(object->get_vrHelpTitle())
                        {
                            setGPRPC2Request->set_vrHelpTitle(*object->get_vrHelpTitle());
                        }

                        setGPRPC2Request->set_appId(sessionKey);
                        HMIHandler::getInstance().sendRequest(setGPRPC2Request);
                    }
                    else
                    {
                        mobileResponse->set_success(false);
                        mobileResponse->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_DATA);
                        MobileHandler::getInstance().sendRPCMessage(mobileResponse, sessionKey);
                    }

                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::ResetGlobalPropertiesID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A ResetGlobalProperties request has been invoked");
                    NsSmartDeviceLinkRPCV2::ResetGlobalProperties_request* object = (NsSmartDeviceLinkRPCV2::ResetGlobalProperties_request*)mobileMsg;
                    NsSmartDeviceLinkRPCV2::ResetGlobalProperties_response * mobileResponse = new NsSmartDeviceLinkRPCV2::ResetGlobalProperties_response;
                    mobileResponse->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ResetGlobalPropertiesID);
                    mobileResponse->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    mobileResponse->setCorrelationID(object->getCorrelationID());
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        mobileResponse->set_success(false);
                        mobileResponse->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(mobileResponse, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        mobileResponse->set_success(false);
                        mobileResponse->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        MobileHandler::getInstance().sendRPCMessage(mobileResponse, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::ResetGlobalProperties* resetGPRPC2Request = new NsRPC2Communication::UI::ResetGlobalProperties();
                    resetGPRPC2Request->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[resetGPRPC2Request->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    resetGPRPC2Request->set_properties(object->get_properties());
                    resetGPRPC2Request->set_appId(sessionKey);
                    HMIHandler::getInstance().sendRequest(resetGPRPC2Request);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::CreateInteractionChoiceSetID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A CreateInteractionChoiceSet request has been invoked");
                    NsSmartDeviceLinkRPCV2::CreateInteractionChoiceSet_request* object = (NsSmartDeviceLinkRPCV2::CreateInteractionChoiceSet_request*)mobileMsg;
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPCV2::CreateInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPCV2::CreateInteractionChoiceSet_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::CreateInteractionChoiceSetID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::CreateInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPCV2::CreateInteractionChoiceSet_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::CreateInteractionChoiceSetID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::CreateInteractionChoiceSet* createInteractionChoiceSet = new NsRPC2Communication::UI::CreateInteractionChoiceSet();
                    createInteractionChoiceSet->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[createInteractionChoiceSet->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());

                    createInteractionChoiceSet->set_choiceSet(object->get_choiceSet());
                    createInteractionChoiceSet->set_interactionChoiceSetID(object->get_interactionChoiceSetID());
                    createInteractionChoiceSet->set_appId(app->getAppID());
                    app->addChoiceSet(object->get_interactionChoiceSetID(), object->get_choiceSet());
                    HMIHandler::getInstance().sendRequest(createInteractionChoiceSet);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::DeleteInteractionChoiceSetID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A DeleteInteractionChoiceSet request has been invoked");
                    NsSmartDeviceLinkRPCV2::DeleteInteractionChoiceSet_request* object = (NsSmartDeviceLinkRPCV2::DeleteInteractionChoiceSet_request*)mobileMsg;

                    if (core->mPerformInteractionFlag == object->get_interactionChoiceSetID())
                    {
                        NsSmartDeviceLinkRPCV2::DeleteInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPCV2::DeleteInteractionChoiceSet_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteInteractionChoiceSetID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::IN_USE);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        return;
                    }

                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPCV2::DeleteInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPCV2::DeleteInteractionChoiceSet_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteInteractionChoiceSetID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::DeleteInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPCV2::DeleteInteractionChoiceSet_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteInteractionChoiceSetID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    const unsigned int& choiceSetId = object->get_interactionChoiceSetID();
                    const ChoiceSetV2* choiceSetFound = app->findChoiceSet(choiceSetId);
                    if(!choiceSetFound)
                    {
                        LOG4CPLUS_WARN(mLogger, " a choice set " << choiceSetId
                                            << " hasn't been registered within the application " << app->getName() << " id" << app->getAppID() << " !");
                        NsSmartDeviceLinkRPCV2::DeleteInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPCV2::DeleteInteractionChoiceSet_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteInteractionChoiceSetID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_ID);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        return;
                    }

                    NsRPC2Communication::UI::DeleteInteractionChoiceSet* deleteInteractionChoiceSet = new NsRPC2Communication::UI::DeleteInteractionChoiceSet();
                    deleteInteractionChoiceSet->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[deleteInteractionChoiceSet->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    deleteInteractionChoiceSet->set_interactionChoiceSetID(object->get_interactionChoiceSetID());
                    deleteInteractionChoiceSet->set_appId(app->getAppID());
                    app->removeChoiceSet(object->get_interactionChoiceSetID());
                    HMIHandler::getInstance().sendRequest(deleteInteractionChoiceSet);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::PerformInteractionID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A PerformInteraction request has been invoked");
                    NsSmartDeviceLinkRPCV2::PerformInteraction_request* object = (NsSmartDeviceLinkRPCV2::PerformInteraction_request*)mobileMsg;
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPCV2::PerformInteraction_response* response = new NsSmartDeviceLinkRPCV2::PerformInteraction_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::PerformInteractionID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL != app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::PerformInteraction_response* response = new NsSmartDeviceLinkRPCV2::PerformInteraction_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::PerformInteractionID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    const std::vector<unsigned int>& choiceSets = object->get_interactionChoiceSetIDList();
                    for(std::vector<unsigned int>::const_iterator it = choiceSets.begin(); it != choiceSets.end(); it++)
                    {
                        const unsigned int& choiceSetId = *it;
                        const ChoiceSetV2* choiceSetFound = app->findChoiceSet(choiceSetId);
                        if(!choiceSetFound)
                        {
                            LOG4CPLUS_WARN(mLogger, " a choice set " << choiceSetId
                                                << " hasn't been registered within the application " << app->getName() << " id" << app->getAppID() << " !");
                            NsSmartDeviceLinkRPCV2::PerformInteraction_response* response = new NsSmartDeviceLinkRPCV2::PerformInteraction_response;
                            response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::PerformInteractionID);
                            response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                            response->set_success(false);
                            response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_DATA);
                            response->setCorrelationID(object->getCorrelationID());
                            MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                            return;
                        }
                    }
                    NsRPC2Communication::UI::PerformInteraction* performInteraction = new NsRPC2Communication::UI::PerformInteraction();
                    performInteraction->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[performInteraction->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    performInteraction->set_initialPrompt(object->get_initialPrompt());
                    performInteraction->set_initialText(object->get_initialText());
                    performInteraction->set_interactionChoiceSetIDList(choiceSets);
                    performInteraction->set_interactionMode(object->get_interactionMode());
                    if(object->get_helpPrompt())
                    {
                        performInteraction->set_helpPrompt(*object->get_helpPrompt());
                    }
                    if(object->get_timeout())
                    {
                        performInteraction->set_timeout(*object->get_timeout());
                    }
                    if(object->get_timeoutPrompt())
                    {
                        performInteraction->set_timeoutPrompt(*object->get_timeoutPrompt());
                    }
                    performInteraction->set_appId(sessionKey);
                    if(object->get_vrHelp())
                    {
                        performInteraction->set_vrHelp(*object->get_vrHelp());
                    }
                    HMIHandler::getInstance().sendRequest(performInteraction);
                    core->mPerformInteractionFlag = choiceSets[0];
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::AlertID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An Alert request has been invoked");
                    NsSmartDeviceLinkRPCV2::Alert_request* object = (NsSmartDeviceLinkRPCV2::Alert_request*)mobileMsg;
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPCV2::Alert_response* response = new NsSmartDeviceLinkRPCV2::Alert_response();
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::AlertID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::Alert_response* response = new NsSmartDeviceLinkRPCV2::Alert_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::AlertID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::Alert* alert = new NsRPC2Communication::UI::Alert();
                    alert->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[alert->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    if(object->get_alertText1())
                    {
                        alert->set_AlertText1(*object->get_alertText1());
                    }
                    if(object->get_alertText2())
                    {
                        alert->set_AlertText2(*object->get_alertText2());
                    }
                    if(object->get_alertText3())
                    {
                        alert->set_AlertText3(*object->get_alertText3());
                    }
                    if(object->get_duration())
                    {
                        alert->set_duration(*object->get_duration());
                    }
                    if(object->get_playTone())
                    {
                        alert->set_playTone(*object->get_playTone());
                    }
                    if(object->get_ttsChunks())
                    {
                        alert->set_ttsChunks(*object->get_ttsChunks());
                    }
                    if(object->get_softButtons())
                    {
                        alert->set_softButtons(*object->get_softButtons());
                    }
                    alert->set_appId(sessionKey);
                    HMIHandler::getInstance().sendRequest(alert);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::ShowID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A Show request has been invoked");
                    LOG4CPLUS_INFO_EXT(mLogger, "message " << mobileMsg->getMethodId() );
                    NsSmartDeviceLinkRPCV2::Show_request* object = (NsSmartDeviceLinkRPCV2::Show_request*)mobileMsg;
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPCV2::Show_response* response = new NsSmartDeviceLinkRPCV2::Show_response();
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ShowID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::Show_response* response = new NsSmartDeviceLinkRPCV2::Show_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ShowID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::Show* showRPC2Request = new NsRPC2Communication::UI::Show();
                    showRPC2Request->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    LOG4CPLUS_INFO_EXT(mLogger, "showrpc2request created");
                    if(object->get_mainField1())
                    {
                        showRPC2Request->set_mainField1(*object->get_mainField1());
                    }
                    if(object->get_mainField2())
                    {
                        showRPC2Request->set_mainField2(*object->get_mainField2());
                    }
                    if(object->get_mainField3())
                    {
                        showRPC2Request->set_mainField3(*object->get_mainField3());
                    }
                    if(object->get_mainField4())
                    {
                        showRPC2Request->set_mainField4(*object->get_mainField4());
                    }
                    if(object->get_mediaClock())
                    {
                        showRPC2Request->set_mediaClock(*object->get_mediaClock());
                    }
                    if(object->get_mediaTrack())
                    {
                        showRPC2Request->set_mediaTrack(*object->get_mediaTrack());
                    }
                    if(object->get_statusBar())
                    {
                        showRPC2Request->set_statusBar(*object->get_statusBar());
                    }
                    if(object->get_graphic())
                    {
                        showRPC2Request->set_graphic(*object->get_graphic());
                    }
                    if(object->get_softButtons())
                    {
                        showRPC2Request->set_softButtons(*object->get_softButtons());
                    }
                    if(object->get_customPresets())
                    {
                        showRPC2Request->set_customPresets(*object->get_customPresets());
                    }
                    if(object->get_alignment())
                    {
                        showRPC2Request->set_alignment(*object->get_alignment());
                    }
                    showRPC2Request->set_appId(sessionKey);
                    LOG4CPLUS_INFO_EXT(mLogger, "Show request almost handled" );
                    core->mMessageChaining[showRPC2Request->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    HMIHandler::getInstance().sendRequest(showRPC2Request);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::SpeakID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A Speak request has been invoked");
                    NsSmartDeviceLinkRPCV2::Speak_request* object = (NsSmartDeviceLinkRPCV2::Speak_request*)mobileMsg;
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPCV2::Speak_response* response = new NsSmartDeviceLinkRPCV2::Speak_response();
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SpeakID);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel()
                        || NsSmartDeviceLinkRPCV2::HMILevel::HMI_BACKGROUND == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::Speak_response* response = new NsSmartDeviceLinkRPCV2::Speak_response;
                        response->set_success(false);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SpeakID);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::TTS::Speak* speakRPC2Request = new NsRPC2Communication::TTS::Speak();
                    speakRPC2Request->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    speakRPC2Request->set_ttsChunks(object->get_ttsChunks());
                    speakRPC2Request->set_appId(sessionKey);
                    core->mMessageChaining[speakRPC2Request->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    HMIHandler::getInstance().sendRequest(speakRPC2Request);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::AddCommandID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An AddCommand request has been invoked");
                    NsSmartDeviceLinkRPCV2::AddCommand_request* object = (NsSmartDeviceLinkRPCV2::AddCommand_request*)mobileMsg;

                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if (!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPCV2::AddCommand_response* response = new NsSmartDeviceLinkRPCV2::AddCommand_response();
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::AddCommandID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if (NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key "
                                       << sessionKey << " has not been activated yet!");
                        NsSmartDeviceLinkRPCV2::AddCommand_response* response = new NsSmartDeviceLinkRPCV2::AddCommand_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::AddCommandID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    const int cmdId = object->get_cmdID();

                    LOG4CPLUS_INFO_EXT(mLogger, " An AddCommand UI request has been invoked");
                    NsRPC2Communication::UI::AddCommand* addCmd = new NsRPC2Communication::UI::AddCommand();
                    addCmd->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    addCmd->set_cmdId(cmdId);
                    addCmd->set_appId(app->getAppID());

                    MessageChaining* chain = NULL;
                    chain = core->addChain(chain, sessionKey, object->getCorrelationID());
                    core->mMessageChaining[addCmd->getId()] = chain;

                    if (object->get_menuParams())
                    {
                        addCmd->set_menuParams(*object->get_menuParams());
                        if (object->get_menuParams()->get_parentID())
                        {
                            const unsigned int& menuId = *object->get_menuParams()->get_parentID();
                            app->addMenuCommand(cmdId, menuId);
                        }
                    }

                    if (object->get_cmdIcon())
                    {
                        NsSmartDeviceLinkRPCV2::Image* cmdIcon = const_cast<NsSmartDeviceLinkRPCV2::Image*>(object->get_cmdIcon());

                        char currentAppPath[FILENAME_MAX];
                        char fullPathToIcon[FILENAME_MAX];

                        memset(currentAppPath, 0, FILENAME_MAX);
                        memset(fullPathToIcon, 0, FILENAME_MAX);

                        getcwd(currentAppPath, FILENAME_MAX);
                        snprintf(fullPathToIcon, FILENAME_MAX - 1, "%s/%s/%s"
                                 , currentAppPath, app->getName().c_str(), cmdIcon->get_value().c_str());

                        LOG4CPLUS_INFO_EXT(mLogger, "Full path to sync file name: " << fullPathToIcon);

                        cmdIcon->set_value(fullPathToIcon);
                        addCmd->set_cmdIcon(*cmdIcon);
                    }

                    CommandType cmdType = CommandType::UI;
                    CommandParams params;
                    params.menuParamsV2 = object->get_menuParams();

                    app->addCommand(cmdId, cmdType, params);
                    app->incrementUnrespondedRequestCount(cmdId);
                    HMIHandler::getInstance().sendRequest(addCmd);

                    if (object->get_vrCommands())
                    {
                        LOG4CPLUS_INFO_EXT(mLogger, " An AddCommand VR request has been invoked");
                        NsRPC2Communication::VR::AddCommand* addCmd = new NsRPC2Communication::VR::AddCommand();
                        addCmd->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                        addCmd->set_vrCommands(*object->get_vrCommands());
                        addCmd->set_cmdId(cmdId);
                        addCmd->set_appId(app->getAppID());

                        CommandType cmdType = CommandType::VR;
                        CommandParams params;
                        params.vrCommands = object->get_vrCommands();

                        app->addCommand(cmdId, cmdType, params);
                        app->incrementUnrespondedRequestCount(cmdId);

                        chain = core->addChain(chain, sessionKey, object->getCorrelationID());
                        core->mMessageChaining[addCmd->getId()] = chain;
                        HMIHandler::getInstance().sendRequest(addCmd);
                    }

                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::DeleteCommandID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A DeleteCommand request has been invoked");
                    NsSmartDeviceLinkRPCV2::DeleteCommand_request* object = (NsSmartDeviceLinkRPCV2::DeleteCommand_request*)mobileMsg;
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPCV2::DeleteCommand_response* response = new NsSmartDeviceLinkRPCV2::DeleteCommand_response();
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteCommandID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::DeleteCommand_response* response = new NsSmartDeviceLinkRPCV2::DeleteCommand_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteCommandID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    CommandTypes cmdTypes = app->getCommandTypes(object->get_cmdID());
                    if(cmdTypes.empty())
                    {
                        NsSmartDeviceLinkRPCV2::DeleteCommand_response* response = new NsSmartDeviceLinkRPCV2::DeleteCommand_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteCommandID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_DATA);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    const unsigned int& cmdId = object->get_cmdID();
                    MessageChaining * chain = 0;
                    for(CommandTypes::iterator it = cmdTypes.begin(); it != cmdTypes.end(); it++)
                    {
                        CommandType cmdType = *it;
                        if(cmdType == CommandType::UI)
                        {
                            LOG4CPLUS_INFO_EXT(mLogger, " A DeleteCommand UI request has been invoked");
                            NsRPC2Communication::UI::DeleteCommand* deleteCmd = new NsRPC2Communication::UI::DeleteCommand();
                            deleteCmd->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                            deleteCmd->set_appId(app->getAppID());
                            deleteCmd->set_cmdId(cmdId);
                            app->removeCommand(cmdId, cmdType);
                            app->incrementUnrespondedRequestCount(cmdId);
                            app->removeMenuCommand(cmdId);
                            chain = core->addChain(chain,
                                sessionKey,
                                object->getCorrelationID());
                            core->mMessageChaining[deleteCmd->getId()] = chain;
                            HMIHandler::getInstance().sendRequest(deleteCmd);
                        }
                        else if(cmdType == CommandType::VR)
                        {
                            LOG4CPLUS_INFO_EXT(mLogger, " A DeleteCommand VR request has been invoked");
                            NsRPC2Communication::VR::DeleteCommand* deleteCmd = new NsRPC2Communication::VR::DeleteCommand();
                            deleteCmd->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                            deleteCmd->set_cmdId(cmdId);
                            deleteCmd->set_appId(app->getAppID());
                            app->removeCommand(cmdId, cmdType);
                            app->incrementUnrespondedRequestCount(cmdId);
                            chain = core->addChain(chain,
                                sessionKey,
                                object->getCorrelationID());
                            core->mMessageChaining[deleteCmd->getId()] = chain;
                            HMIHandler::getInstance().sendRequest(deleteCmd);
                        }
                    }
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::AddSubMenuID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An AddSubmenu request has been invoked");
                    NsSmartDeviceLinkRPCV2::AddSubMenu_request* object = (NsSmartDeviceLinkRPCV2::AddSubMenu_request*)mobileMsg;
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPCV2::AddSubMenu_response* response = new NsSmartDeviceLinkRPCV2::AddSubMenu_response();
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::AddSubMenuID);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key "
                                       << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::AddSubMenu_response* response = new NsSmartDeviceLinkRPCV2::AddSubMenu_response;
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::AddSubMenuID);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    const MenuValue* menu = app->findMenu(object->get_menuID());
                    if (menu)
                    {
                        NsSmartDeviceLinkRPCV2::AddSubMenu_response* response = new NsSmartDeviceLinkRPCV2::AddSubMenu_response;
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::AddSubMenuID);
                        response->set_success(false);
                        response->setCorrelationID(object->getCorrelationID());

                        if (menu->first == object->get_menuName())
                        {
                            response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::DUPLICATE_NAME);
                            MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                            return;
                        }

                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_ID);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        return;
                    }

                    NsRPC2Communication::UI::AddSubMenu* addSubMenu = new NsRPC2Communication::UI::AddSubMenu();
                    addSubMenu->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[addSubMenu->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    addSubMenu->set_menuId(object->get_menuID());
                    addSubMenu->set_menuName(object->get_menuName());
                    if(object->get_position())
                    {
                        addSubMenu->set_position(*object->get_position());
                    }
                    addSubMenu->set_appId(app->getAppID());
                    app->addMenu(object->get_menuID(), object->get_menuName(), object->get_position());
                    HMIHandler::getInstance().sendRequest(addSubMenu);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::DeleteSubMenuID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A DeleteSubmenu request has been invoked");
                    NsSmartDeviceLinkRPCV2::DeleteSubMenu_request* object = (NsSmartDeviceLinkRPCV2::DeleteSubMenu_request*)mobileMsg;
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        NsSmartDeviceLinkRPCV2::DeleteSubMenu_response* response = new NsSmartDeviceLinkRPCV2::DeleteSubMenu_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteSubMenuID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::DeleteSubMenu_response* response = new NsSmartDeviceLinkRPCV2::DeleteSubMenu_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteSubMenuID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    const unsigned int& menuId = object->get_menuID();
                    if(!app->findMenu(menuId))
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " menuId " << menuId
                                            << " hasn't been associated with the application " << app->getName() << " id " << app->getAppID() << " !");
                        NsSmartDeviceLinkRPCV2::DeleteSubMenu_response* response = new NsSmartDeviceLinkRPCV2::DeleteSubMenu_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteSubMenuID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::INVALID_ID);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::DeleteSubMenu* delSubMenu = new NsRPC2Communication::UI::DeleteSubMenu();
                    delSubMenu->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[delSubMenu->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    delSubMenu->set_menuId(menuId);
                    delSubMenu->set_appId(app->getAppID());
                    HMIHandler::getInstance().sendRequest(delSubMenu);
                    core->menuMapping[delSubMenu->getId()] = menuId;
                    // app->removeMenu(menuId);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::PerformAudioPassThruID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A PerformAudioPassThru request has been invoked");

                    NsSmartDeviceLinkRPCV2::PerformAudioPassThru_request* request
                        = static_cast<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_request*>(mobileMsg);

                    if (core->getAudioPassThruFlag())
                    {
                        LOG4CPLUS_WARN(mLogger, "PerformAudioPassThru::TOO_MANY_PENDING_REQUESTS");
                        sendResponse<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response
                            , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(mobileMsg
                                , NsSmartDeviceLinkRPCV2::Result::TOO_MANY_PENDING_REQUESTS
                                , false
                                , sessionKey);
                        break;
                    }

                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response* response = new NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response();
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::PerformAudioPassThruID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(request->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response* response = new NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::PerformAudioPassThruID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(request->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    core->setAudioPassThruFlag(true);

                    NsRPC2Communication::UI::PerformAudioPassThru* performAudioPassThru
                        = new NsRPC2Communication::UI::PerformAudioPassThru;
                    if (!performAudioPassThru)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "PerformAudioPassThru::OUT_OF_MEMORY");
                        sendResponse<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response
                            , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(mobileMsg
                                , NsSmartDeviceLinkRPCV2::Result::OUT_OF_MEMORY
                                , false
                                , sessionKey);
                        core->setAudioPassThruFlag(false);
                        break;
                    }

                    const std::vector<NsSmartDeviceLinkRPCV2::TTSChunk>& initialPrompt = request->get_initialPrompt();
                    const NsSmartDeviceLinkRPCV2::SamplingRate& samplingRate = request->get_samplingRate();
                    unsigned int maxDuration = request->get_maxDuration();
                    const NsSmartDeviceLinkRPCV2::AudioCaptureQuality& bitsPerSample = request->get_bitsPerSample();
                    const NsSmartDeviceLinkRPCV2::AudioType& audioType = request->get_audioType();

                    performAudioPassThru->set_initialPrompt(initialPrompt);
                    performAudioPassThru->set_samplingRate(samplingRate);
                    performAudioPassThru->set_maxDuration(maxDuration);
                    performAudioPassThru->set_bitsPerSample(bitsPerSample);
                    performAudioPassThru->set_audioType(audioType);

                    const std::string* firstDisplayText = request->get_audioPassThruDisplayText1();
                    if(firstDisplayText)
                        performAudioPassThru->set_audioPassThruDisplayText1(*firstDisplayText);

                    const std::string* secondDisplayText = request->get_audioPassThruDisplayText2();
                    if (secondDisplayText)
                        performAudioPassThru->set_audioPassThruDisplayText2(*secondDisplayText);

                    performAudioPassThru->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    performAudioPassThru->set_appId(app->getAppID());
                    core->mMessageChaining[performAudioPassThru->getId()] = new MessageChaining(
                        sessionKey,
                        request->getCorrelationID());
                    HMIHandler::getInstance().sendRequest(performAudioPassThru);
                    LOG4CPLUS_INFO_EXT(mLogger, "Request PerformAudioPassThru sent to HMI ...");

                    AudioPassThruData* data = new AudioPassThruData;
                    if (!data)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "PerformAudioPassThru::OUT_OF_MEMORY");
                        sendResponse<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response
                            , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(NsSmartDeviceLinkRPCV2::FunctionID::PerformAudioPassThruID
                                , NsSmartDeviceLinkRPCV2::Result::OUT_OF_MEMORY
                                , request->getCorrelationID()
                                , false
                                , sessionKey);
                        core->setAudioPassThruFlag(false);
                        break;
                    }

                    //  Thread for audio record operation
                    data->samplingRate = samplingRate;
                    data->maxDuration = maxDuration;
                    data->bitsPerSample = bitsPerSample;
                    data->audioType = audioType;
                    data->sessionKey = sessionKey;
                    data->id = request->getCorrelationID();
                    pthread_create(&audioPassThruThread, 0, AudioPassThru, static_cast<void*>(data));

                    LOG4CPLUS_INFO_EXT(mLogger, "AudioPassThru thread created...");

                    // We send response only when we finish or EndAudioPassThru request received.
                    // Look for AudioPassThru thread proc and EndAudioPassThruID event handler.
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::EndAudioPassThruID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A EndAudioPassThru request has been invoked");

                    pthread_cond_signal(&cv);
                    pthread_cancel(audioPassThruThread);

                    Application_v2* app
                        = getApplicationV2AndCheckHMIStatus<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response>(sessionKey,
                            mobileMsg);
                    if (!app)
                        break;

                    NsRPC2Communication::UI::EndAudioPassThru* endAudioPassThru
                        = new NsRPC2Communication::UI::EndAudioPassThru;
                    if (!endAudioPassThru)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "EndAudioPassThru::OUT_OF_MEMORY");
                        sendResponse<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response
                            , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(mobileMsg
                                , NsSmartDeviceLinkRPCV2::Result::OUT_OF_MEMORY
                                , false
                                , sessionKey);
                        core->setAudioPassThruFlag(false);
                        break;
                    }

                    endAudioPassThru->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    endAudioPassThru->set_appId(app->getAppID());
                    core->mMessageChaining[endAudioPassThru->getId()] = new MessageChaining(
                        sessionKey,
                        mobileMsg->getCorrelationID());
                    HMIHandler::getInstance().sendRequest(endAudioPassThru);
                    LOG4CPLUS_INFO_EXT(mLogger, "Request EndAudioPassThru sent to HMI ...");
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::SubscribeVehicleDataID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A SubscribeVehicleData request has been invoked");
                    NsSmartDeviceLinkRPCV2::SubscribeVehicleData_request * object = static_cast<NsSmartDeviceLinkRPCV2::SubscribeVehicleData_request*>(mobileMsg);
                    NsSmartDeviceLinkRPCV2::SubscribeVehicleData_response* response = new NsSmartDeviceLinkRPCV2::SubscribeVehicleData_response();
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SubscribeVehicleDataID);
                    response->setCorrelationID(object->getCorrelationID());
                    /*RegistryItem* item = AppMgrRegistry::getInstance().getItem(sessionKey);
                    if(!item)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }*/
                    Application_v2* app = (Application_v2*)core->getItem( sessionKey );
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::GENERIC_ERROR);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    std::vector<NsSmartDeviceLinkRPCV2::VehicleDataType> vdVector = object->get_dataType();
                    int countOfItems = vdVector.size();
                    std::vector<NsSmartDeviceLinkRPCV2::VehicleDataType>::iterator it;
                    for (it = vdVector.begin(); it != vdVector.end(); it++)
                    {
                        if (core->mVehicleDataMapping.addVehicleDataMapping(*it, app))
                        {
                            countOfItems--;
                        }
                    }
                    if (0 == countOfItems)
                    {
                        response->set_success(true);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                    } else if (countOfItems == vdVector.size())
                    {
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->set_info("Application is already subscribed on all VehicleData which it is trying to subscribe!");
                    } else if (countOfItems < vdVector.size())
                    {
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::WARNINGS);
                        response->set_info("Application was already subscribed on some VehicleData which it is trying to subscribe!");
                    } else
                    {
                        LOG4CPLUS_WARN(mLogger, "Wrong command sequence!" );
                        break;
                    }
                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::UnsubscribeVehicleDataID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " An UnsubscribeVehicleData request has been invoked");
                    NsSmartDeviceLinkRPCV2::UnsubscribeVehicleData_request * object = static_cast<NsSmartDeviceLinkRPCV2::UnsubscribeVehicleData_request*>(mobileMsg);
                    NsSmartDeviceLinkRPCV2::UnsubscribeVehicleData_response* response = new NsSmartDeviceLinkRPCV2::UnsubscribeVehicleData_response();
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::UnsubscribeVehicleDataID);
                    response->setCorrelationID(object->getCorrelationID());
                    /*RegistryItem* item = AppMgrRegistry::getInstance().getItem(sessionKey);
                    if(!item)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " session key " << sessionKey
                            << " hasn't been associated with any application!");
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }*/
                    Application_v2* app = (Application_v2*)core->getItem( sessionKey );
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::GENERIC_ERROR);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    std::vector<NsSmartDeviceLinkRPCV2::VehicleDataType> vdVector = object->get_dataType();
                    int countOfItems = vdVector.size();
                    std::vector<NsSmartDeviceLinkRPCV2::VehicleDataType>::iterator it;
                    for (it = vdVector.begin(); it != vdVector.end(); it++)
                    {
                        if (core->mVehicleDataMapping.removeVehicleDataMapping(*it, app))
                        {
                            countOfItems--;
                        }
                    }
                    if (0 == countOfItems)
                    {
                        response->set_success(true);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                    } else if (countOfItems == vdVector.size())
                    {
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->set_info("Application was not subscribed on any VehicleData which it is trying to unsubscribe!");
                    } else if (countOfItems < vdVector.size())
                    {
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::WARNINGS);
                        response->set_info("Application was subscribed not to all VehicleData which it is trying to unsubscribe!");
                    } else
                    {
                        LOG4CPLUS_WARN(mLogger, "Wrong command sequence!" );
                        break;
                    }
                    MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::GetVehicleDataID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A GetVehicleData request has been invoked");
                    NsSmartDeviceLinkRPCV2::GetVehicleData_request* object = static_cast<NsSmartDeviceLinkRPCV2::GetVehicleData_request*>(mobileMsg);
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPCV2::GetVehicleData_response* response = new NsSmartDeviceLinkRPCV2::GetVehicleData_response();
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::GetVehicleDataID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::GetVehicleData_response* response = new NsSmartDeviceLinkRPCV2::GetVehicleData_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::GetVehicleDataID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::VehicleInfo::GetVehicleData* getVehicleDataRPC2Request = new NsRPC2Communication::VehicleInfo::GetVehicleData();
                    getVehicleDataRPC2Request->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    LOG4CPLUS_INFO_EXT(mLogger, "getVehicleDataRPC2Request created");
                    getVehicleDataRPC2Request->set_dataType(object->get_dataType());
                    getVehicleDataRPC2Request->set_appId(sessionKey);
                    LOG4CPLUS_INFO_EXT(mLogger, "GetVehicleData request almost handled" );
                    core->mMessageChaining[getVehicleDataRPC2Request->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    HMIHandler::getInstance().sendRequest(getVehicleDataRPC2Request);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::ReadDIDID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, "ReadDID is received from mobile app.");
                    NsSmartDeviceLinkRPCV2::ReadDID_request * request = static_cast<NsSmartDeviceLinkRPCV2::ReadDID_request*>(mobileMsg);
                    NsAppManager::Application_v2* app = static_cast<NsAppManager::Application_v2*>(core->
                            getItem(sessionKey));
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "An application " << app->getName() << " is not registered." );
                        NsSmartDeviceLinkRPCV2::ReadDID_response* response = new NsSmartDeviceLinkRPCV2::ReadDID_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ReadDIDID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(request->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::ReadDID_response* response = new NsSmartDeviceLinkRPCV2::ReadDID_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ReadDIDID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(request->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    NsRPC2Communication::VehicleInfo::ReadDID * readDIDRequest =
                        new NsRPC2Communication::VehicleInfo::ReadDID;
                    readDIDRequest->set_appId(app->getAppID());
                    readDIDRequest->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    core->mMessageChaining[readDIDRequest->getId()] = new MessageChaining(
                        sessionKey,
                        request->getCorrelationID());
                    readDIDRequest->set_ecuName(request->get_ecuName());
                    readDIDRequest->set_didLocation(request->get_didLocation());
                    if ( request->get_encrypted() )
                    {
                        readDIDRequest->set_encrypted( *request->get_encrypted() );
                    }
                    HMIHandler::getInstance().sendRequest(readDIDRequest);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::GetDTCsID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A GetDTCs request has been invoked");
                    NsSmartDeviceLinkRPCV2::GetDTCs_request* object = static_cast<NsSmartDeviceLinkRPCV2::GetDTCs_request*>(mobileMsg);
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPCV2::GetDTCs_response* response = new NsSmartDeviceLinkRPCV2::GetDTCs_response();
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::GetDTCsID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::GetDTCs_response* response = new NsSmartDeviceLinkRPCV2::GetDTCs_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::GetDTCsID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::VehicleInfo::GetDTCs* getDTCsRPC2Request = new NsRPC2Communication::VehicleInfo::GetDTCs();
                    getDTCsRPC2Request->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    LOG4CPLUS_INFO_EXT(mLogger, "getDTCsRPC2Request created");
                    getDTCsRPC2Request->set_ecuName(object->get_ecuName());
                    if (object->get_encrypted())
                    {
                        getDTCsRPC2Request->set_encrypted(*(object->get_encrypted()));
                    }
                    getDTCsRPC2Request->set_appId(sessionKey);
                    LOG4CPLUS_INFO_EXT(mLogger, "GetDTCs request almost handled" );
                    core->mMessageChaining[getDTCsRPC2Request->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    HMIHandler::getInstance().sendRequest(getDTCsRPC2Request);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::ChangeRegistrationID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, "A ChangeRegistration request has been invoked." );

                    NsAppManager::Application_v2* app = static_cast<NsAppManager::Application_v2*>(
                                core->getItem(sessionKey));
                    if(!app)
                    {
                        sendResponse<NsSmartDeviceLinkRPCV2::ChangeRegistration_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                            mobileMsg
                            , NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED
                            , false
                            , sessionKey);
                        break;
                    }

                    NsSmartDeviceLinkRPCV2::ChangeRegistration_request* request
                        = static_cast<NsSmartDeviceLinkRPCV2::ChangeRegistration_request*>(mobileMsg);

                    // -------------------------------------------------------------------------------------------------
                    // Is it supported HMI, VR and TTS language?
                    NsSmartDeviceLinkRPCV2::Language hmiLanguage = request->get_hmiDisplayLanguage();
                    std::vector<NsSmartDeviceLinkRPCV2::Language>::iterator it = std::find(
                        core->mUISupportedLanguages.begin(), core->mUISupportedLanguages.end(), hmiLanguage);
                    if (it == core->mUISupportedLanguages.end())
                    {
                        sendResponse<NsSmartDeviceLinkRPCV2::ChangeRegistration_response
                            , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                                NsSmartDeviceLinkRPCV2::FunctionID::ChangeRegistrationID
                                , NsSmartDeviceLinkRPCV2::Result::INVALID_DATA
                                , request->getCorrelationID()
                                , false
                                , sessionKey);
                        return;
                    }

                    NsSmartDeviceLinkRPCV2::Language language = request->get_language();
                    it = std::find(core->mVRSupportedLanguages.begin(), core->mVRSupportedLanguages.end(), language);
                    if (it == core->mVRSupportedLanguages.end())
                    {
                        sendResponse<NsSmartDeviceLinkRPCV2::ChangeRegistration_response
                            , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                                NsSmartDeviceLinkRPCV2::FunctionID::ChangeRegistrationID
                                , NsSmartDeviceLinkRPCV2::Result::INVALID_DATA
                                , request->getCorrelationID()
                                , false
                                , sessionKey);
                        return;
                    }

                    it = std::find(core->mTTSSupportedLanguages.begin(), core->mTTSSupportedLanguages.end(),
                        language);
                    if (it == core->mTTSSupportedLanguages.end())
                    {
                        sendResponse<NsSmartDeviceLinkRPCV2::ChangeRegistration_response
                            , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                                NsSmartDeviceLinkRPCV2::FunctionID::ChangeRegistrationID
                                , NsSmartDeviceLinkRPCV2::Result::INVALID_DATA
                                , request->getCorrelationID()
                                , false
                                , sessionKey);
                        return;
                    }
                    // -------------------------------------------------------------------------------------------------

                    bool hasActuallyChanged = false;
                    MessageChaining * chain = 0;
                    if ( app->getHMIDisplayLanguageDesired().get() != request->get_hmiDisplayLanguage().get())
                    {
                        app->setHMIDisplayLanguageDesired(request->get_hmiDisplayLanguage());

                        if (app->getApplicationHMIStatusLevel() == NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL)
                        {
                            NsRPC2Communication::UI::ChangeRegistration * changeUIRegistration =
                                new NsRPC2Communication::UI::ChangeRegistration;
                            changeUIRegistration->set_hmiDisplayLanguage(request->get_hmiDisplayLanguage());
                            changeUIRegistration->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                            changeUIRegistration->set_appId(app->getAppID());
                            chain = core->addChain(chain,
                                sessionKey,
                                request->getCorrelationID());
                            core->mMessageChaining[changeUIRegistration->getId()] = chain;
                            HMIHandler::getInstance().sendRequest(changeUIRegistration);
                        }
                        else if ( app->getApplicationHMIStatusLevel() == NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE )
                        {
                            sendResponse<NsSmartDeviceLinkRPCV2::ChangeRegistration_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                                mobileMsg
                                , NsSmartDeviceLinkRPCV2::Result::SUCCESS
                                , true
                                , sessionKey);
                        }
                        hasActuallyChanged = true;
                    }
                    if (app->getLanguageDesired().get() != request->get_language().get())
                    {
                        app->setLanguageDesired(request->get_language());

                        if (app->getApplicationHMIStatusLevel() != NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE)
                        {
                            NsRPC2Communication::VR::ChangeRegistration * changeVrRegistration =
                                new NsRPC2Communication::VR::ChangeRegistration;
                            changeVrRegistration->set_language(request->get_language());
                            changeVrRegistration->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                            changeVrRegistration->set_appId(app->getAppID());
                            chain = core->addChain(chain,
                                sessionKey,
                                request->getCorrelationID());
                            core->mMessageChaining[changeVrRegistration->getId()] = chain;
                            HMIHandler::getInstance().sendRequest(changeVrRegistration);

                            NsRPC2Communication::TTS::ChangeRegistration * changeTtsRegistration =
                                new NsRPC2Communication::TTS::ChangeRegistration;
                            changeTtsRegistration->set_language(request->get_language());
                            changeTtsRegistration->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                            changeTtsRegistration->set_appId(app->getAppID());
                            app->incrementUnrespondedRequestCount(app->getAppID());
                            chain = core->addChain(chain,
                                sessionKey,
                                request->getCorrelationID());
                            core->mMessageChaining[changeTtsRegistration->getId()] = chain;
                            HMIHandler::getInstance().sendRequest(changeTtsRegistration);
                        }
                        else
                        {
                            sendResponse<NsSmartDeviceLinkRPCV2::ChangeRegistration_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                                mobileMsg
                                , NsSmartDeviceLinkRPCV2::Result::SUCCESS
                                , true
                                , sessionKey);
                        }
                        hasActuallyChanged = true;
                    }

                    if ( !hasActuallyChanged )
                    {
                        sendResponse<NsSmartDeviceLinkRPCV2::ChangeRegistration_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                            mobileMsg
                            , NsSmartDeviceLinkRPCV2::Result::SUCCESS
                            , true
                            , sessionKey);
                    }
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::AlertManeuverID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, "A AlertManeuver request has been invoked." );
                    sendResponse<NsSmartDeviceLinkRPCV2::AlertManeuver_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                            mobileMsg
                            , NsSmartDeviceLinkRPCV2::Result::IGNORED
                            , false
                            , sessionKey);

                    /*NsSmartDeviceLinkRPCV2::AlertManeuver_request* request
                        = static_cast<NsSmartDeviceLinkRPCV2::AlertManeuver_request*>(mobileMsg);

                    Application_v2* app = static_cast<Application_v2*>(
                        core->getItem(sessionKey)));
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger,
                            "No application associated with the registry item with session key " << sessionKey );
                        sendResponse<NsSmartDeviceLinkRPCV2::AlertManeuver_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                            NsSmartDeviceLinkRPCV2::FunctionID::AlertManeuverID
                            , NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED
                            , NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE
                            , false
                            , sessionKey);
                        break;
                    }

                    if((NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                        || (NsSmartDeviceLinkRPCV2::HMILevel::HMI_BACKGROUND == app->getApplicationHMIStatusLevel()))
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "An application " << app->getName()
                            << " with session key " << sessionKey << " has not been activated yet!" );
                        sendResponse<NsSmartDeviceLinkRPCV2::AlertManeuver_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                            NsSmartDeviceLinkRPCV2::FunctionID::AlertManeuverID
                            , NsSmartDeviceLinkRPCV2::Result::REJECTED
                            , NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE
                            , false
                            , sessionKey);
                        break;
                    }

                    NsRPC2Communication::UI::AlertManeuver* alert
                        = new NsRPC2Communication::UI::AlertManeuver;
                    if (!alert)
                    {
                        LOG4CPLUS_INFO_EXT(mLogger, "new NsRPC2Communication::Navigation::AlertManeuver() failed");
                        sendResponse<NsSmartDeviceLinkRPCV2::AlertManeuver_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                            NsSmartDeviceLinkRPCV2::FunctionID::AlertManeuverID
                            , NsSmartDeviceLinkRPCV2::Result::OUT_OF_MEMORY
                            , NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE
                            , false
                            , sessionKey);
                        break;
                    }

                    alert->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    alert->set_appId(app->getAppID());
                    alert->set_ttsChunks(request->get_ttsChunks());
                    alert->set_softButtons(request->get_softButtons());

                    core->mMessageMapping.addMessage(alert->getId(), sessionKey);
                    HMIHandler::getInstance().sendRequest(alert);*/
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::DialNumberID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " A DialNumber request has been invoked");
                    NsSmartDeviceLinkRPCV2::DialNumber_request* object = static_cast<NsSmartDeviceLinkRPCV2::DialNumber_request*>(mobileMsg);
                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPCV2::DialNumber_response* response = new NsSmartDeviceLinkRPCV2::DialNumber_response();
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DialNumberID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL != app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::DialNumber_response* response = new NsSmartDeviceLinkRPCV2::DialNumber_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DialNumberID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(object->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    NsRPC2Communication::UI::DialNumber* dialNumberRPC2Request = new NsRPC2Communication::UI::DialNumber();
                    dialNumberRPC2Request->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    LOG4CPLUS_INFO_EXT(mLogger, "dialNumberRPC2Request created");
                    dialNumberRPC2Request->set_number(object->get_number());
                    dialNumberRPC2Request->set_appId(sessionKey);
                    LOG4CPLUS_INFO_EXT(mLogger, "DialNumber request almost handled" );
                    core->mMessageChaining[dialNumberRPC2Request->getId()] = new MessageChaining(
                        sessionKey,
                        object->getCorrelationID());
                    HMIHandler::getInstance().sendRequest(dialNumberRPC2Request);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::ShowConstantTBTID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, "A ShowConstantTBT request has been invoked." );
                    NsSmartDeviceLinkRPCV2::ShowConstantTBT_request* request
                        = static_cast<NsSmartDeviceLinkRPCV2::ShowConstantTBT_request*>(mobileMsg);

                    Application_v2* app = (Application_v2*)core->getItem(sessionKey);
                    if(!app)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item with session key " << sessionKey );
                        NsSmartDeviceLinkRPCV2::ShowConstantTBT_response* response = new NsSmartDeviceLinkRPCV2::ShowConstantTBT_response();
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ShowConstantTBTID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED);
                        response->setCorrelationID(request->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }
                    if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_LIMITED == app->getApplicationHMIStatusLevel()
                        || NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == app->getApplicationHMIStatusLevel())
                    {
                        LOG4CPLUS_WARN(mLogger, "An application " << app->getName() << " with session key " << sessionKey << " has not been activated yet!" );
                        NsSmartDeviceLinkRPCV2::ShowConstantTBT_response* response = new NsSmartDeviceLinkRPCV2::ShowConstantTBT_response;
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ShowConstantTBTID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(false);
                        response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::REJECTED);
                        response->setCorrelationID(request->getCorrelationID());
                        MobileHandler::getInstance().sendRPCMessage(response, sessionKey);
                        break;
                    }

                    NsRPC2Communication::UI::ShowConstantTBT* showConstantTBT
                        = new NsRPC2Communication::UI::ShowConstantTBT;
                    if (!showConstantTBT)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "new NsRPC2Communication::UI::ShowConstantTBT() failed");
                        sendResponse<NsSmartDeviceLinkRPCV2::ShowConstantTBT_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                            mobileMsg
                            , NsSmartDeviceLinkRPCV2::Result::OUT_OF_MEMORY
                            , false
                            , sessionKey);
                        return;
                    }

                    showConstantTBT->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    showConstantTBT->set_appId(app->getAppID());
                    showConstantTBT->set_softButtons(request->get_softButtons());
                    showConstantTBT->set_distanceToManeuver(request->get_distanceToManeuver());
                    showConstantTBT->set_distanceToManeuverScale(request->get_distanceToManeuverScale());
                    showConstantTBT->set_turnIcon(request->get_turnIcon());

                    const std::string* firstNavigationText = request->get_navigationText1();
                    const std::string* secondNavigationText = request->get_navigationText2();
                    const std::string* eta = request->get_eta();
                    const std::string* totalDistance = request->get_totalDistance();
                    const bool* maneuverComplete = request->get_maneuverComplete();

                    if (firstNavigationText)
                        showConstantTBT->set_navigationText1(*firstNavigationText);
                    if (secondNavigationText)
                        showConstantTBT->set_navigationText2(*secondNavigationText);
                    if (eta)
                        showConstantTBT->set_eta(*eta);
                    if (totalDistance)
                        showConstantTBT->set_totalDistance(*totalDistance);
                    if (maneuverComplete)
                        showConstantTBT->set_maneuverComplete(maneuverComplete);

                    core->mMessageChaining[showConstantTBT->getId()] = new MessageChaining(
                        sessionKey,
                        request->getCorrelationID());
                    HMIHandler::getInstance().sendRequest(showConstantTBT);
                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::UpdateTurnListID:
                {
                    LOG4CPLUS_INFO_EXT(mLogger, "A UpdateTurnList request has been invoked." );
                    NsSmartDeviceLinkRPCV2::UpdateTurnList_request* request
                        = static_cast<NsSmartDeviceLinkRPCV2::UpdateTurnList_request*>(mobileMsg);

                    Application_v2* app = getApplicationV2AndCheckHMIStatus<NsSmartDeviceLinkRPCV2::ShowConstantTBT_response>(
                        sessionKey,
                        request);
                    if (!app)
                        return;

                    NsRPC2Communication::UI::UpdateTurnList* updateTurnList = new NsRPC2Communication::UI::UpdateTurnList;
                    if (!updateTurnList)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, "new NsRPC2Communication::UI::UpdateTurnList() failed");
                        sendResponse<NsSmartDeviceLinkRPCV2::UpdateTurnList_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                            mobileMsg
                            , NsSmartDeviceLinkRPCV2::Result::OUT_OF_MEMORY
                            , false
                            , sessionKey);
                        return;
                    }

                    updateTurnList->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    updateTurnList->set_appId(app->getAppID());
                    std::vector<NsSmartDeviceLinkRPCV2::Turn> toHMI;
                    const std::vector<NsSmartDeviceLinkRPCV2::Turn> fromMobile = request->get_turnList();
                    std::vector<NsSmartDeviceLinkRPCV2::Turn>::const_iterator i = fromMobile.begin();
                    for (i; i != fromMobile.end(); ++i)
                    {
                        NsSmartDeviceLinkRPCV2::Turn turn;
                        turn.set_navigationText(*(i->get_navigationText()));

                        NsSmartDeviceLinkRPCV2::Image image;
                        image.set_imageType(i->get_turnIcon().get_imageType());

                        std::string relativeFilePath = app->getName();
                        relativeFilePath += "/";
                        relativeFilePath += i->get_turnIcon().get_value();

                        std::string fullFilePath = WorkWithOS::getFullPath( relativeFilePath );
                        if (!WorkWithOS::checkIfFileExists(fullFilePath))
                        {
                            LOG4CPLUS_ERROR_EXT(mLogger, "UpdateTurnList file doesn't exist");
                                sendResponse<NsSmartDeviceLinkRPCV2::UpdateTurnList_response
                                    , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                                        NsSmartDeviceLinkRPCV2::FunctionID::UpdateTurnListID
                                            , NsSmartDeviceLinkRPCV2::Result::INVALID_DATA
                                            , request->getCorrelationID()
                                            , false
                                            , sessionKey);
                            return;
                        }

                        image.set_value(fullFilePath);
                        turn.set_turnIcon(image);
                        toHMI.push_back(turn);
                    }

                    updateTurnList->set_turnList(toHMI);
                    updateTurnList->set_softButtons(request->get_softButtons());

                    core->mMessageChaining[updateTurnList->getId()] = new MessageChaining(
                        sessionKey,
                        request->getCorrelationID());
                    HMIHandler::getInstance().sendRequest(updateTurnList);

                    break;
                }
                case NsSmartDeviceLinkRPCV2::FunctionID::INVALID_ENUM:
                default:
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, " An undefined or invalid RPC message " << mobileMsg->getMethodId()
                        << " has been received!");

                    sendResponse<NsSmartDeviceLinkRPCV2::GenericResponse_response
                        , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                            NsSmartDeviceLinkRPCV2::FunctionID::GenericResponseID
                                , NsSmartDeviceLinkRPCV2::Result::INVALID_DATA
                                , mobileMsg->getCorrelationID()
                                , false
                                , sessionKey);
                    break;
                }
            }
        }
    }

    /**
     * \brief push HMI RPC2 message to a queue
     * \param msg a message to be pushed
     * \param pThis a pointer to AppMgrCore class instance
     */
    void AppMgrCore::handleBusRPCMessageIncoming(NsRPC2Communication::RPC2Command* msg , void *pThis)
    {
        if(!msg)
        {
            LOG4CPLUS_ERROR_EXT(mLogger, " Incoming null pointer from HMI side!");
            return;
        }
        LOG4CPLUS_INFO_EXT(mLogger, " A RPC2 bus message " << msg->getMethod() << " has been incoming...");

        if(!pThis)
        {
            LOG4CPLUS_ERROR_EXT(mLogger, " pThis should point to an instance of AppMgrCore class");
            return;
        }

        AppMgrCore* core = (AppMgrCore*)pThis;
        switch(msg->getMethod())
        {
            case NsRPC2Communication::Marshaller::METHOD_INVALID:
            {
                LOG4CPLUS_ERROR_EXT(mLogger, " An invalid RPC message " << msg->getMethod() << " has been received!");
                return;
            }
            default:
                LOG4CPLUS_INFO_EXT(mLogger, " A valid RPC message " << msg->getMethod() << " has been received!");
        }

        switch(msg->getMethod())
        {
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_BUTTONS__ONBUTTONEVENT:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " An OnButtonEvent notification has been invoked");
                NsRPC2Communication::Buttons::OnButtonEvent * object = (NsRPC2Communication::Buttons::OnButtonEvent*)msg;

                NsSmartDeviceLinkRPCV2::ButtonName btnName;
                btnName.set((NsSmartDeviceLinkRPCV2::ButtonName::ButtonNameInternal)object->get_name().get());

                if(object->get_customButtonID())
                {
                    LOG4CPLUS_INFO_EXT(mLogger, "No subscription for custom buttons is required.");
                    Application* app = core->getActiveItem();
                    if (!app)
                    {
                        LOG4CPLUS_WARN_EXT(mLogger, "OnButtonPress came but no app is active.");
                        return;
                    }
                    core->sendButtonEvent(app, object);
                    return;
                }

                ResultRange subscribedApps = core->mButtonsMapping.findSubscribedToButton(btnName);

                for( ButtonMap::const_iterator it=subscribedApps.first; it!=subscribedApps.second; ++it )
                {
                    Application* app = it -> second;
                    if (!app)
                    {
                        LOG4CPLUS_WARN(mLogger, "Null pointer to subscribed app.");
                        continue;
                    }

                    if ( NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL != app->getApplicationHMIStatusLevel()
                            && NsSmartDeviceLinkRPCV2::HMILevel::HMI_LIMITED != app->getApplicationHMIStatusLevel() )
                    {
                        LOG4CPLUS_WARN_EXT(mLogger, "Application is not supposed \
                            to receive OnButtonPress when in HMI_BACKGROUND or NONE");
                        continue;
                    }
                    core->sendButtonEvent(app, object);
                }

                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_BUTTONS__ONBUTTONPRESS:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " An OnButtonPress notification has been invoked");
                NsRPC2Communication::Buttons::OnButtonPress * object = (NsRPC2Communication::Buttons::OnButtonPress*)msg;
                const NsSmartDeviceLinkRPC::ButtonName & name = static_cast<NsSmartDeviceLinkRPC::ButtonName::ButtonNameInternal>(object->get_name().get());
                NsSmartDeviceLinkRPCV2::ButtonName btnName;
                btnName.set((NsSmartDeviceLinkRPCV2::ButtonName::ButtonNameInternal)name.get());

                if(object->get_customButtonID())
                {
                    LOG4CPLUS_INFO_EXT(mLogger, "No subscription for custom buttons is required.");
                    Application* app = core->getActiveItem();
                    if (!app)
                    {
                        LOG4CPLUS_WARN_EXT(mLogger, "OnButtonPress came but no app is active.");
                        return;
                    }
                    core->sendButtonPress(app, object);
                    return;
                }

                ResultRange subscribedApps = core->mButtonsMapping.findSubscribedToButton(btnName);

                for( ButtonMap::const_iterator it=subscribedApps.first; it!=subscribedApps.second; ++it )
                {
                    Application* app = it -> second;
                    if (!app)
                    {
                        LOG4CPLUS_WARN(mLogger, "Null pointer to subscribed app.");
                        continue;
                    }

                    if ( NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL != app->getApplicationHMIStatusLevel()
                            && NsSmartDeviceLinkRPCV2::HMILevel::HMI_LIMITED != app->getApplicationHMIStatusLevel() )
                    {
                        LOG4CPLUS_WARN_EXT(mLogger, "Application is not supposed \
                            to receive OnButtonPress when in HMI_BACKGROUND or NONE");
                        continue;
                    }
                    core->sendButtonPress(app, object);
                }

                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_BUTTONS__GETCAPABILITIESRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A GetButtonCapabilities response has been income");
                NsRPC2Communication::Buttons::GetCapabilitiesResponse * btnCaps = (NsRPC2Communication::Buttons::GetCapabilitiesResponse*)msg;
                core->mButtonCapabilitiesV2.set( btnCaps->get_capabilities() );
                if(btnCaps->get_presetBankCapabilities())
                {
                    core->mPresetBankCapabilities = *btnCaps->get_presetBankCapabilities();
                }

                --core->mHMIStartupFlag;
                if (!core->mHMIStartupFlag)
                {
                    ConnectionHandler::getInstance().startTransportManager();
                }
                return;
            }
            default:
                LOG4CPLUS_INFO_EXT(mLogger, " Not Buttons RPC message " << msg->getMethod() << " has been received!");
        }

        switch(msg->getMethod())
        {
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__ONREADY:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " An OnReady UI notification has been invoked");
                HMIHandler::getInstance().setReadyState(true);

                NsRPC2Communication::UI::GetCapabilities* getUiCapsRequest = new NsRPC2Communication::UI::GetCapabilities();
                getUiCapsRequest->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                HMIHandler::getInstance().sendRequest(getUiCapsRequest);
                NsRPC2Communication::VR::GetCapabilities* getVrCapsRequest = new NsRPC2Communication::VR::GetCapabilities();
                getVrCapsRequest->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                HMIHandler::getInstance().sendRequest(getVrCapsRequest);
                NsRPC2Communication::TTS::GetCapabilities* getTtsCapsRequest = new NsRPC2Communication::TTS::GetCapabilities();
                getTtsCapsRequest->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                HMIHandler::getInstance().sendRequest(getTtsCapsRequest);
                NsRPC2Communication::Buttons::GetCapabilities* getButtonsCapsRequest = new NsRPC2Communication::Buttons::GetCapabilities();
                getButtonsCapsRequest->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                HMIHandler::getInstance().sendRequest(getButtonsCapsRequest);
                NsRPC2Communication::VehicleInfo::GetVehicleType* getVehicleType = new NsRPC2Communication::VehicleInfo::GetVehicleType;
                getVehicleType->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                HMIHandler::getInstance().sendRequest(getVehicleType);

                NsRPC2Communication::UI::GetSupportedLanguages * getUISupportedLanguages
                    = new NsRPC2Communication::UI::GetSupportedLanguages;
                getUISupportedLanguages->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                HMIHandler::getInstance().sendRequest(getUISupportedLanguages);

                NsRPC2Communication::TTS::GetSupportedLanguages * getTTSSupportedLanguages
                    = new NsRPC2Communication::TTS::GetSupportedLanguages;
                getTTSSupportedLanguages->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                HMIHandler::getInstance().sendRequest(getTTSSupportedLanguages);

                NsRPC2Communication::VR::GetSupportedLanguages* getVRSupportedLanguages
                    = new NsRPC2Communication::VR::GetSupportedLanguages;
                getVRSupportedLanguages->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                HMIHandler::getInstance().sendRequest(getVRSupportedLanguages);

                NsRPC2Communication::UI::GetLanguage* getUiLang = new NsRPC2Communication::UI::GetLanguage;
                getUiLang->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                HMIHandler::getInstance().sendRequest(getUiLang);
                NsRPC2Communication::VR::GetLanguage* getVrLang = new NsRPC2Communication::VR::GetLanguage;
                getVrLang->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                HMIHandler::getInstance().sendRequest(getVrLang);
                NsRPC2Communication::TTS::GetLanguage* getTtsLang = new NsRPC2Communication::TTS::GetLanguage;
                getTtsLang->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                HMIHandler::getInstance().sendRequest(getTtsLang);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__PERFORMAUDIOPASSTHRURESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A PerformAudioPassThru response has been invoked");

                pthread_cond_signal(&cv);
                pthread_cancel(audioPassThruThread);

                NsRPC2Communication::UI::PerformAudioPassThruResponse* response
                    = static_cast<NsRPC2Communication::UI::PerformAudioPassThruResponse*>(msg);

                MessageChains::iterator it = core->mMessageChaining.find(response->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << response->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No application associated with this registry item!");
                    NsAppManager::AppMgrCore::getInstance().setAudioPassThruFlag(false);
                    return;
                }

                sendResponse<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response
                        , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(NsSmartDeviceLinkRPCV2::FunctionID::PerformAudioPassThruID
                        , static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(response->getResult())
                        , it->second->correlationID
                        , true
                        , it->second->connectionKey);

                // We wait for new PerformAudioPassThru request.
                core -> decreaseMessageChain(it);
                NsAppManager::AppMgrCore::getInstance().setAudioPassThruFlag(false);
                break;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__ENDAUDIOPASSTHRURESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A EndAudioPassThru response has been invoked");
                NsRPC2Communication::UI::EndAudioPassThruResponse* response =
                    static_cast<NsRPC2Communication::UI::EndAudioPassThruResponse*>(msg);

                MessageChains::iterator it = core->mMessageChaining.find(response->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << response->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    NsAppManager::AppMgrCore::getInstance().setAudioPassThruFlag(false);
                    return;
                }

                sendResponse<NsSmartDeviceLinkRPCV2::EndAudioPassThru_response
                    , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(NsSmartDeviceLinkRPCV2::FunctionID::EndAudioPassThruID
                        , static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(response->getResult())
                        , it->second->correlationID
                        , NsSmartDeviceLinkRPCV2::Result::SUCCESS == static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                            response->getResult())
                        , app->getAppID());

                /*sendResponse<NsSmartDeviceLinkRPCV2::PerformAudioPassThru_response
                    , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(NsSmartDeviceLinkRPCV2::FunctionID::PerformAudioPassThruID
                        , static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(response->getResult())
                        , NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE
                        , NsSmartDeviceLinkRPCV2::Result::SUCCESS == static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                            response->getResult())
                        , app->getAppID());*/
                core->decreaseMessageChain(it);
                core->setAudioPassThruFlag(false);

                break;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__GETCAPABILITIESRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A GetUICapabilities response has been income");
                NsRPC2Communication::UI::GetCapabilitiesResponse * uiCaps = (NsRPC2Communication::UI::GetCapabilitiesResponse*)msg;
                core->mDisplayCapabilitiesV2 = uiCaps->get_displayCapabilities();

                NsSmartDeviceLinkRPC::DisplayCapabilities displayCaps;
                const NsSmartDeviceLinkRPCV2::DisplayCapabilities& displayCapsV2 = uiCaps->get_displayCapabilities();
                displayCaps.set_displayType(
                    static_cast<NsSmartDeviceLinkRPC::DisplayType::DisplayTypeInternal>(
                        displayCapsV2.get_displayType().get()));
                std::vector<NsSmartDeviceLinkRPC::MediaClockFormat> mediaClockV1;
                convert<NsSmartDeviceLinkRPC::MediaClockFormat,
                        NsSmartDeviceLinkRPCV2::MediaClockFormat,
                        NsSmartDeviceLinkRPC::MediaClockFormat::MediaClockFormatInternal>(
                    displayCapsV2.get_mediaClockFormats(),
                    mediaClockV1);
                displayCaps.set_mediaClockFormats(mediaClockV1);

                std::vector<NsSmartDeviceLinkRPC::TextField> txtFields;
                for(std::vector<NsSmartDeviceLinkRPCV2::TextField>::const_iterator it = displayCapsV2.get_textFields().begin();
                        it != displayCapsV2.get_textFields().end(); it++)
                {
                    NsSmartDeviceLinkRPC::TextField txtField;
                    const NsSmartDeviceLinkRPCV2::TextField txtFieldOrig = *it;
                    NsSmartDeviceLinkRPC::CharacterSet charset;
                    charset.set(
                        static_cast<NsSmartDeviceLinkRPC::CharacterSet::CharacterSetInternal>(
                            txtFieldOrig.get_characterSet().get())
                            );
                    txtField.set_characterSet(charset);
                    txtField.set_name(
                        static_cast<NsSmartDeviceLinkRPC::TextFieldName::TextFieldNameInternal>(
                            txtFieldOrig.get_name().get()));
                    txtField.set_rows(txtFieldOrig.get_rows());
                    txtField.set_width(txtFieldOrig.get_width());
                    txtFields.push_back(txtField);
                }
                displayCaps.set_textFields(txtFields);
                core->mDisplayCapabilitiesV1 = displayCaps;
                core->mHmiZoneCapabilitiesV2.set( uiCaps->get_hmiZoneCapabilities() );
                if(uiCaps->get_softButtonCapabilities())
                {
                    core->mSoftButtonCapabilities.set(*uiCaps->get_softButtonCapabilities());
                }

                --core->mHMIStartupFlag;
                if (!core->mHMIStartupFlag)
                {
                    ConnectionHandler::getInstance().startTransportManager();
                }

                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__GETLANGUAGERESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "GetLanguageResponse from UI is received");
                NsRPC2Communication::UI::GetLanguageResponse* getLang = (NsRPC2Communication::UI::GetLanguageResponse*)msg;
                core->mUiLanguageV1.set(
                    static_cast<NsSmartDeviceLinkRPC::Language::LanguageInternal>(
                        getLang->get_hmiDisplayLanguage().get()));
                core->mUiLanguageV2 = getLang->get_hmiDisplayLanguage();

                --core->mHMIStartupFlag;
                if (!core->mHMIStartupFlag)
                {
                    ConnectionHandler::getInstance().startTransportManager();
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__ONCOMMAND:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " An OnCommand UI notification has been invoked");
                NsRPC2Communication::UI::OnCommand* object = (NsRPC2Communication::UI::OnCommand*)msg;
                Application* app = core->getApplicationByCommand(object->get_commandId(), object->get_appId());
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::OnCommand* event = new NsSmartDeviceLinkRPC::OnCommand();
                        event->set_cmdID(object->get_commandId());
                        event->set_triggerSource(NsSmartDeviceLinkRPC::TriggerSource::TS_MENU);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(event, appId);
                        break;
                    }
                    case 2:
                    {
                        NsSmartDeviceLinkRPCV2::OnCommand* event = new NsSmartDeviceLinkRPCV2::OnCommand();
                        event->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnCommandID);
                        event->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                        event->set_cmdID(object->get_commandId());
                        event->set_triggerSource(NsSmartDeviceLinkRPCV2::TriggerSource::TS_MENU);
                        event->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnCommandID);
                        event->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(event, appId);
                        break;
                    }
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__SHOWRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A Show response has been income");
                NsRPC2Communication::UI::ShowResponse* object = (NsRPC2Communication::UI::ShowResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::Show_response* response = new NsSmartDeviceLinkRPC::Show_response();
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->set_success(true);
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                    case 2:
                    {
                        NsSmartDeviceLinkRPCV2::Show_response* response = new NsSmartDeviceLinkRPCV2::Show_response();
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ShowID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->set_success(true);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ShowID);
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                }
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__SETGLOBALPROPERTIESRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A SetGlobalProperties response has been income");
                NsRPC2Communication::UI::SetGlobalPropertiesResponse* object = (NsRPC2Communication::UI::SetGlobalPropertiesResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::SetGlobalProperties_response* response = new NsSmartDeviceLinkRPC::SetGlobalProperties_response();

                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->set_success(true);
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                    case 2:
                    {
                        NsSmartDeviceLinkRPCV2::SetGlobalProperties_response* response = new NsSmartDeviceLinkRPCV2::SetGlobalProperties_response();

                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SetGlobalPropertiesID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->set_success(true);
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                }
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__RESETGLOBALPROPERTIESRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A ResetGlobalProperties response has been income");
                NsRPC2Communication::UI::ResetGlobalPropertiesResponse* object = (NsRPC2Communication::UI::ResetGlobalPropertiesResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::ResetGlobalProperties_response* response = new NsSmartDeviceLinkRPC::ResetGlobalProperties_response();

                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(true);
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                    case 2:
                    {
                        NsSmartDeviceLinkRPCV2::ResetGlobalProperties_response* response = new NsSmartDeviceLinkRPCV2::ResetGlobalProperties_response();

                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ResetGlobalPropertiesID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(true);
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                }
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__ALERTRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " An Alert response has been income");
                NsRPC2Communication::UI::AlertResponse* object = (NsRPC2Communication::UI::AlertResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::Alert_response* response = new NsSmartDeviceLinkRPC::Alert_response();
                        response->set_success(true);
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                    case 2:
                    {
                        NsSmartDeviceLinkRPCV2::Alert_response* response = new NsSmartDeviceLinkRPCV2::Alert_response();
                        response->set_success(true);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::AlertID);
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        if(object->get_tryAgainTime())
                        {
                            response->set_tryAgainTime(*object->get_tryAgainTime());
                        }
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                        break;
                    }
                }
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__ADDCOMMANDRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " An AddCommand UI response has been income");
                NsRPC2Communication::UI::AddCommandResponse* object = (NsRPC2Communication::UI::AddCommandResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int connectionKey = it->second->connectionKey;
                //unsigned int cmdId = core->mRequestMapping.findRequestIdAssignedToMessage(object->getId());
                //app->decrementUnrespondedRequestCount(cmdId);
                unsigned int correlationID = it->second->correlationID;
                if(core->decreaseMessageChain(it)) //app->getUnrespondedRequestCount(cmdId) == 0
                {
                    switch(app->getProtocolVersion())
                    {
                        case 1:
                        {
                            NsSmartDeviceLinkRPC::AddCommand_response* response = new NsSmartDeviceLinkRPC::AddCommand_response();
                            response->set_success(true);
                            response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                            response->setCorrelationID(correlationID);
                            LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                                << " application id " << connectionKey);
                            MobileHandler::getInstance().sendRPCMessage(response, connectionKey);
                            break;
                        }
                        case 2:
                        {
                            NsSmartDeviceLinkRPCV2::AddCommand_response* response = new NsSmartDeviceLinkRPCV2::AddCommand_response();
                            response->set_success(true);
                            response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                            response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::AddCommandID);
                            response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                            response->setCorrelationID(correlationID);
                            LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                                << " application id " << connectionKey);
                            MobileHandler::getInstance().sendRPCMessage(response, connectionKey);
                            break;
                        }
                    }
                }

                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__DELETECOMMANDRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A DeleteCommand UI response has been income");
                NsRPC2Communication::UI::DeleteCommandResponse* object = (NsRPC2Communication::UI::DeleteCommandResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int connectionKey = it->second->connectionKey;
                unsigned int correlationID = it->second->correlationID;
                if(core->decreaseMessageChain(it))
                {
                    switch(app->getProtocolVersion())
                    {
                        case 1:
                        {
                            NsSmartDeviceLinkRPC::DeleteCommand_response* response = new NsSmartDeviceLinkRPC::DeleteCommand_response();
                            response->set_success(true);
                            response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                            response->setCorrelationID(correlationID);
                            LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                                << " application id " << connectionKey);
                            MobileHandler::getInstance().sendRPCMessage(response, connectionKey);
                            break;
                        }
                        case 2:
                        {
                            NsSmartDeviceLinkRPCV2::DeleteCommand_response* response = new NsSmartDeviceLinkRPCV2::DeleteCommand_response();
                            response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteCommandID);
                            response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                            response->set_success(true);
                            response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                            response->setCorrelationID(correlationID);
                            LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                                << " application id " << connectionKey);
                            MobileHandler::getInstance().sendRPCMessage(response, connectionKey);
                            break;
                        }
                    }
                }

                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__ADDSUBMENURESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " An AddSubMenu response has been income");
                NsRPC2Communication::UI::AddSubMenuResponse* object = (NsRPC2Communication::UI::AddSubMenuResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::AddSubMenu_response* response = new NsSmartDeviceLinkRPC::AddSubMenu_response();
                        response->set_success(true);
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                    case 2:
                    {
                        NsSmartDeviceLinkRPCV2::AddSubMenu_response* response = new NsSmartDeviceLinkRPCV2::AddSubMenu_response();
                        response->set_success(true);
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::AddSubMenuID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                }
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__DELETESUBMENURESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A DeleteSubMenu response has been income");
                NsRPC2Communication::UI::DeleteSubMenuResponse* object = (NsRPC2Communication::UI::DeleteSubMenuResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::DeleteSubMenu_response* response = new NsSmartDeviceLinkRPC::DeleteSubMenu_response();
                        response->set_success(true);
                        if (NsSmartDeviceLinkRPCV2::Result::SUCCESS !=
                                static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()))
                        {
                            response->set_success(false);
                        }
                        else
                        {
                            std::map<int, int>::iterator menuId = core->menuMapping.find(object->getId());
                            if (menuId != core->menuMapping.end())
                            {
                                app->removeMenu(menuId->second);
                                core->menuMapping.erase(menuId);
                            }
                        }
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                    case 2:
                    {
                        NsSmartDeviceLinkRPCV2::DeleteSubMenu_response* response = new NsSmartDeviceLinkRPCV2::DeleteSubMenu_response();
                        response->set_success(true);
                        if (NsSmartDeviceLinkRPCV2::Result::SUCCESS !=
                                static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()))
                        {
                            response->set_success(false);
                        }
                        else
                        {
                            std::map<int, int>::iterator menuId = core->menuMapping.find(object->getId());
                            if (menuId != core->menuMapping.end())
                            {
                                app->removeMenu(menuId->second);
                                core->menuMapping.erase(menuId);
                            }
                        }
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteSubMenuID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                }
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__CREATEINTERACTIONCHOICESETRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A CreateInteractionChoiceSet response has been income");
                NsRPC2Communication::UI::CreateInteractionChoiceSetResponse* object = (NsRPC2Communication::UI::CreateInteractionChoiceSetResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::CreateInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPC::CreateInteractionChoiceSet_response();
                        response->set_success(true);
                        if (NsSmartDeviceLinkRPCV2::Result::SUCCESS !=
                                static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()))
                        {
                            response->set_success(false);
                        }
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                    case 2:
                    {
                        NsSmartDeviceLinkRPCV2::CreateInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPCV2::CreateInteractionChoiceSet_response();
                        response->set_success(true);
                        if (NsSmartDeviceLinkRPCV2::Result::SUCCESS !=
                                static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()))
                        {
                            response->set_success(false);
                        }
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::CreateInteractionChoiceSetID);
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                }
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__DELETEINTERACTIONCHOICESETRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A DeleteInteractionChoiceSet response has been income");
                NsRPC2Communication::UI::DeleteInteractionChoiceSetResponse* object = (NsRPC2Communication::UI::DeleteInteractionChoiceSetResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::DeleteInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPC::DeleteInteractionChoiceSet_response();
                        response->set_success(true);
                        if (NsSmartDeviceLinkRPCV2::Result::SUCCESS !=
                                static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()))
                        {
                            response->set_success(false);
                        }
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                    case 2:
                    {
                        NsSmartDeviceLinkRPCV2::DeleteInteractionChoiceSet_response* response = new NsSmartDeviceLinkRPCV2::DeleteInteractionChoiceSet_response();
                        response->set_success(true);
                        if (NsSmartDeviceLinkRPCV2::Result::SUCCESS !=
                                static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()))
                        {
                            response->set_success(false);
                        }
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteInteractionChoiceSetID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                }
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__PERFORMINTERACTIONRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A PerformInteraction response has been income");
                NsRPC2Communication::UI::PerformInteractionResponse* object = (NsRPC2Communication::UI::PerformInteractionResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::PerformInteraction_response* response = new NsSmartDeviceLinkRPC::PerformInteraction_response();
                        if(object->get_choiceID())
                        {
                            response->set_choiceID(*object->get_choiceID());
                        }
                        if(object->get_triggerSource())
                        {
                            response->set_triggerSource(
                                static_cast<NsSmartDeviceLinkRPC::TriggerSource::TriggerSourceInternal>(
                                    object->get_triggerSource()->get()));
                        }
                        response->set_success(true);
                        if (NsSmartDeviceLinkRPCV2::Result::SUCCESS !=
                                static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()))
                        {
                            response->set_success(false);
                        }
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        core->mPerformInteractionFlag = -1;
                        break;
                    }
                    case 2:
                    {
                        NsSmartDeviceLinkRPCV2::PerformInteraction_response* response = new NsSmartDeviceLinkRPCV2::PerformInteraction_response();
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::PerformInteractionID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        if(object->get_choiceID())
                        {
                            response->set_choiceID(*object->get_choiceID());
                        }
                        if(object->get_triggerSource())
                        {
                            NsSmartDeviceLinkRPCV2::TriggerSource triggerSrc;
                            triggerSrc.set((NsSmartDeviceLinkRPCV2::TriggerSource::TriggerSourceInternal)object->get_triggerSource()->get());
                            response->set_triggerSource(triggerSrc);
                        }
                        response->set_success(true);
                        if (NsSmartDeviceLinkRPCV2::Result::SUCCESS !=
                                static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()))
                        {
                            response->set_success(false);
                        }
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        core->mPerformInteractionFlag = -1;
                        break;
                    }
                }
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__SETMEDIACLOCKTIMERRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A SetMediaClockTimer response has been income");
                NsRPC2Communication::UI::SetMediaClockTimerResponse* object = (NsRPC2Communication::UI::SetMediaClockTimerResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::SetMediaClockTimer_response* response = new NsSmartDeviceLinkRPC::SetMediaClockTimer_response();
                        response->set_success(true);
                        if (NsSmartDeviceLinkRPCV2::Result::SUCCESS !=
                                static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()))
                        {
                            response->set_success(false);
                        }
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                    case 2:
                    {
                        NsSmartDeviceLinkRPCV2::SetMediaClockTimer_response* response = new NsSmartDeviceLinkRPCV2::SetMediaClockTimer_response();
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SetMediaClockTimerID);
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_success(true);
                        if (NsSmartDeviceLinkRPCV2::Result::SUCCESS !=
                                static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()))
                        {
                            response->set_success(false);
                        }
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(response, appId);
                        break;
                    }
                }
                core -> decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__ONDRIVERDISTRACTION:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " An OnDriverDistraction UI notification has been invoked");
                NsRPC2Communication::UI::OnDriverDistraction* object
                    = static_cast<NsRPC2Communication::UI::OnDriverDistraction*>(msg);

                for(std::map<int,Application*>::const_iterator it = core->mApplications.begin();
                    it != core->mApplications.end();
                    ++it)
                {
                    if (NULL != it->second)
                    {
                        NsSmartDeviceLinkRPCV2::HMILevel::HMILevelInternal hmiLevel = it->second->getApplicationHMIStatusLevel();
                        if(NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL == hmiLevel
                            || NsSmartDeviceLinkRPCV2::HMILevel::HMI_BACKGROUND == hmiLevel)
                        {
                            int appId = it->second->getAppID();

                            // We need two events simultaneously, because we may have applications of more than
                            // one protocol version registered on the HMI and all they need to be notified of
                            // an OnDriverDistraction event
                            NsSmartDeviceLinkRPC::OnDriverDistraction* eventV1 = new NsSmartDeviceLinkRPC::OnDriverDistraction();
                            eventV1->set_state(
                                static_cast<NsSmartDeviceLinkRPC::DriverDistractionState::DriverDistractionStateInternal>(
                                    object->get_state().get()));
                            core->mDriverDistractionV1 = eventV1;
                            NsSmartDeviceLinkRPCV2::OnDriverDistraction* eventV2 = new NsSmartDeviceLinkRPCV2::OnDriverDistraction();
                            eventV2->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnDriverDistractionID);
                            eventV2->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);

                            NsSmartDeviceLinkRPCV2::DriverDistractionState stateV2;
                            stateV2.set(
                                static_cast<NsSmartDeviceLinkRPCV2::DriverDistractionState::DriverDistractionStateInternal>(
                                    object->get_state().get()));
                            eventV2->set_state(stateV2);
                            eventV2->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnDriverDistractionID);
                            eventV2->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                            core->mDriverDistractionV2 = eventV2;

                            switch(it->second->getProtocolVersion())
                            {
                                case 1:
                                {
                                    MobileHandler::getInstance().sendRPCMessage(eventV1, appId);
                                    break;
                                }
                                case 2:
                                {
                                    MobileHandler::getInstance().sendRPCMessage(eventV2, appId);
                                    break;
                                }
                            }
                        }
                    }
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__ONSYSTEMCONTEXT:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " An OnSystemContext UI notification has been invoked");
                NsRPC2Communication::UI::OnSystemContext* object = (NsRPC2Communication::UI::OnSystemContext*)msg;

                Application* app = core->getActiveItem();
                if(!app)
                {
                    LOG4CPLUS_INFO_EXT(mLogger, " null-application found as an active item!");
                    return;
                }

                if ( (NsSmartDeviceLinkRPCV2::SystemContext::SYSCTXT_HMI_OBSCURED == object->get_systemContext().get() ||
                     NsSmartDeviceLinkRPCV2::SystemContext::SYSCTXT_ALERT == object->get_systemContext().get()) &&
                     1 == app->getProtocolVersion() )
                {
                    LOG4CPLUS_INFO_EXT(mLogger, "This system context is not supported for first generation of RPC.");
                    return;
                }

                if ( NsSmartDeviceLinkRPCV2::SystemContext::NOT_AUDIBLE == object->get_systemContext().get() )
                {
                    if (app->getApplicationAudioStreamingState().get() == NsSmartDeviceLinkRPCV2::AudioStreamingState::AUDIBLE)
                    {
                        app->setApplicationAudioStreamingState(NsSmartDeviceLinkRPCV2::AudioStreamingState::NOT_AUDIBLE);
                        core->sendHMINotificationToMobile(app);
                    }
                    return;
                }
                else
                {
                    app->setSystemContext(object->get_systemContext());
                    app->setApplicationAudioStreamingState(NsSmartDeviceLinkRPCV2::AudioStreamingState::AUDIBLE);
                    core->sendHMINotificationToMobile(app);
                    return;
                }

                if( app->getSystemContext().get() != object->get_systemContext().get() )
                {
                    app->setSystemContext(object->get_systemContext());

                    if ( NsSmartDeviceLinkRPCV2::SystemContext::SYSCTXT_MAIN == object->get_systemContext().get()
                        && NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL != app->getApplicationHMIStatusLevel() )
                    {
                        core->performActivitiesForActivatingApp(app);
                    }
                }
                else
                {
                    core->sendHMINotificationToMobile(app);
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__SLIDERRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A Slider response has been income");
                NsRPC2Communication::UI::SliderResponse* uiResponse = (NsRPC2Communication::UI::SliderResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(uiResponse->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << uiResponse->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();
                NsSmartDeviceLinkRPCV2::Slider_response* response = new NsSmartDeviceLinkRPCV2::Slider_response();

                response->set_success(true);
                if (NsSmartDeviceLinkRPCV2::Result::SUCCESS !=
                        static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(uiResponse->getResult()))
                {
                    response->set_success(false);
                }
                response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SliderID);
                response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                response->set_sliderPosition(uiResponse->get_sliderPosition());
                response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(uiResponse->getResult()));
                response->setCorrelationID(it->second->correlationID);

                LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                    << " application id " << appId);
                MobileHandler::getInstance().sendRPCMessage(response, appId);
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__SETAPPICONRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A SetAppIcon response has been income");
                NsRPC2Communication::UI::SetAppIconResponse* uiResponse = static_cast<NsRPC2Communication::UI::SetAppIconResponse*>(msg);

                MessageChains::iterator it = core->mMessageChaining.find(uiResponse->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << uiResponse->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                NsSmartDeviceLinkRPCV2::Result::ResultInternal resultCode
                    = static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(uiResponse->getResult());

                NsSmartDeviceLinkRPCV2::SetAppIcon_response* response = new NsSmartDeviceLinkRPCV2::SetAppIcon_response();
                response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SetAppIconID);
                response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                response->set_resultCode(resultCode);
                response->setCorrelationID(it->second->correlationID);
                NsSmartDeviceLinkRPCV2::Result::SUCCESS == resultCode ? response->set_success(true) : response->set_success(false);

                MobileHandler::getInstance().sendRPCMessage(response, it->second->connectionKey);
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__SCROLLABLEMESSAGERESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A ScrollableMessageID response has been income");
                NsRPC2Communication::UI::ScrollableMessageResponse* uiResponse
                    = static_cast<NsRPC2Communication::UI::ScrollableMessageResponse*>(msg);

                MessageChains::iterator it = core->mMessageChaining.find(uiResponse->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << uiResponse->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();
                NsSmartDeviceLinkRPCV2::Result::ResultInternal resultCode
                    = static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(uiResponse->getResult());

                NsSmartDeviceLinkRPCV2::ScrollableMessage_response* response = new NsSmartDeviceLinkRPCV2::ScrollableMessage_response();
                if (!response)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "Can't create scrollable message response object");
                    return;
                }
                response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ScrollableMessageID);
                response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                response->set_resultCode(resultCode);
                response->setCorrelationID(it->second->correlationID);
                NsSmartDeviceLinkRPCV2::Result::SUCCESS == resultCode ? response->set_success(true) : response->set_success(false);

                MobileHandler::getInstance().sendRPCMessage(response, it->second->connectionKey);
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__ONDEVICECHOSEN:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " An OnDeviceChosen notification has been income");
                NsRPC2Communication::UI::OnDeviceChosen* chosen = (NsRPC2Communication::UI::OnDeviceChosen*)msg;
                const std::string& deviceName = chosen->get_deviceName();
                //const NsConnectionHandler::CDevice* device = core->mDeviceList.findDeviceByName(deviceName);
                for(std::map<int, DeviceStorage>::const_iterator it = core->mDevices.begin();
                        it != core->mDevices.end();
                        ++it)
                {
                    if ( !it->second.getUserFriendlyName().compare(deviceName) )
                    {
                        ConnectionHandler::getInstance().connectToDevice(it->first);
                        return;
                    }
                }/*
                if (device)
                {
                    const NsConnectionHandler::tDeviceHandle& handle = device->getDeviceHandle();
                    ConnectionHandler::getInstance().connectToDevice(handle);
                }*/
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__GETSUPPORTEDLANGUAGESRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "Get Supported Languages for UI response is received.");
                NsRPC2Communication::UI::GetSupportedLanguagesResponse * languages =
                    static_cast<NsRPC2Communication::UI::GetSupportedLanguagesResponse*>(msg);
                if (NsSmartDeviceLinkRPCV2::Result::SUCCESS == languages->getResult())
                {
                    core->mUISupportedLanguages = languages->get_languages();
                }

                --core->mHMIStartupFlag;
                if (!core->mHMIStartupFlag)
                {
                    ConnectionHandler::getInstance().startTransportManager();
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__ONLANGUAGECHANGE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "UI::OnLanguageChange is received from HMI.");
                NsRPC2Communication::UI::OnLanguageChange * languageChange =
                    static_cast<NsRPC2Communication::UI::OnLanguageChange*>(msg);
                if ( languageChange->get_hmiDisplayLanguage().get() != core->mUiLanguageV2.get() )
                {
                    // TODO(PV): clear mess around versions up.
                    core->mUiLanguageV2 = languageChange->get_hmiDisplayLanguage();
                    core->mUiLanguageV1.set(static_cast<NsSmartDeviceLinkRPC::Language::LanguageInternal>(languageChange->get_hmiDisplayLanguage().get()));

                    /*const AppMgrRegistry::ItemsMap & allRegisteredApplications = AppMgrRegistry::getInstance().getItems();
                    for( AppMgrRegistry::ItemsMap::const_iterator it = allRegisteredApplications.begin();
                            it != allRegisteredApplications.end();
                            ++it )*/
                    for(std::map<int,Application*>::const_iterator it = core->mApplications.begin();
                                it != core->mApplications.end();
                                ++it)
                    {
                        if ( 0 != it->second )
                        {
                            if (NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE
                                != it->second->getApplicationHMIStatusLevel() &&
                                    1 != it->second->getProtocolVersion() )
                            {
                                NsSmartDeviceLinkRPCV2::OnLanguageChange * languageChangeToApp =
                                    new NsSmartDeviceLinkRPCV2::OnLanguageChange;
                                languageChangeToApp->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                                languageChangeToApp->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnLanguageChangeID);
                                languageChangeToApp->set_hmiDisplayLanguage(core->mUiLanguageV2);
                                languageChangeToApp->set_language(core->mVrLanguageV2);
                                MobileHandler::getInstance().sendRPCMessage(languageChangeToApp, it->first);
                            }
                        }
                    }
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__CHANGEREGISTRATIONRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "UI::ChangeRegistrationResponse is received from HMI.");
                NsRPC2Communication::UI::ChangeRegistrationResponse * response =
                    static_cast<NsRPC2Communication::UI::ChangeRegistrationResponse*>(msg);
                MessageChains::iterator it = core->mMessageChaining.find(response->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << response->getId());
                    return;
                }
                Application_v2* app = static_cast<Application_v2*>(
                    core->getItem(it->second->connectionKey));
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                // TODO: exchange when result is not succes.
                if(core->decreaseMessageChain(it))
                {
                    if ( NsSmartDeviceLinkRPCV2::Result::SUCCESS != response->getResult() )
                    {
                        sendResponse<NsSmartDeviceLinkRPCV2::ChangeRegistration_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                                NsSmartDeviceLinkRPCV2::FunctionID::ChangeRegistrationID
                                , static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(response->getResult())
                                , it->second->correlationID
                                , false
                                , it->second->connectionKey);
                        // TODO(PV): not sure if this is correct behaviour
                        app->setHMIDisplayLanguageDesired(core->mUiLanguageV2);
                    }
                    else
                    {
                        sendResponse<NsSmartDeviceLinkRPCV2::ChangeRegistration_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                                NsSmartDeviceLinkRPCV2::FunctionID::ChangeRegistrationID
                                , static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(response->getResult())
                                , it->second->correlationID
                                , true
                                , it->second->connectionKey);
                    }
                    LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                        << " application id " << it->second->connectionKey);
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__SHOWCONSTANTTBTRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "UI::ShowConstantTBTResponse is received from HMI.");
                NsRPC2Communication::UI::ShowConstantTBTResponse * response =
                    static_cast<NsRPC2Communication::UI::ShowConstantTBTResponse*>(msg);

                MessageChains::iterator it = core->mMessageChaining.find(response->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << response->getId());
                    return;
                }
                Application_v2* app = static_cast<Application_v2*>(
                    core->getItem(it->second->connectionKey));
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                sendResponse<NsSmartDeviceLinkRPCV2::ShowConstantTBT_response
                    , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                        NsSmartDeviceLinkRPCV2::FunctionID::ShowConstantTBTID
                        , static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(response->getResult())
                        , it->second->correlationID
                        , response->getResult() == NsSmartDeviceLinkRPCV2::Result::SUCCESS
                        , it->second->connectionKey);

                core->decreaseMessageChain(it);
                break;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__UPDATETURNLISTRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "UI::UpdateTurnListResponse is received from HMI.");
                NsRPC2Communication::UI::UpdateTurnListResponse * response =
                    static_cast<NsRPC2Communication::UI::UpdateTurnListResponse*>(msg);

                MessageChains::iterator it = core->mMessageChaining.find(response->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << response->getId());
                    return;
                }
                Application_v2* app = static_cast<Application_v2*>(core->getItem(it->second->connectionKey));
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                sendResponse<NsSmartDeviceLinkRPCV2::UpdateTurnList_response
                    , NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                        NsSmartDeviceLinkRPCV2::FunctionID::UpdateTurnListID
                        , static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(response->getResult())
                        , it->second->correlationID
                        , response->getResult() == NsSmartDeviceLinkRPCV2::Result::SUCCESS
                        , it->second->connectionKey);

                core->decreaseMessageChain(it);
                break;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__DIALNUMBERRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A DialNambor response has been income");
                NsRPC2Communication::UI::DialNumberResponse* object = static_cast<NsRPC2Communication::UI::DialNumberResponse*>(msg);
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                if (2 == app->getProtocolVersion())
                {
                    NsSmartDeviceLinkRPCV2::DialNumber_response* response = new NsSmartDeviceLinkRPCV2::DialNumber_response();
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DialNumberID);
                    NsSmartDeviceLinkRPCV2::Result::ResultInternal result = static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult());
                    response->set_resultCode(result);
                    NsSmartDeviceLinkRPCV2::Result::SUCCESS == static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult())
                                    ? response->set_success(true) : response->set_success(false);
                    response->setCorrelationID(it->second->correlationID);
                    LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName());
                    MobileHandler::getInstance().sendRPCMessage(response, it->second->connectionKey);
                } else
                {
                    LOG4CPLUS_WARN(mLogger, "This command is available in protocol V2 only!");
                }
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__ONTBTCLIENTSTATE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "UI::OnTBTClientState is received from HMI.");
                NsRPC2Communication::UI::OnTBTClientState * notification =
                    static_cast<NsRPC2Communication::UI::OnTBTClientState*>(msg);

                Application* app = core->getItem(notification->get_appId());
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                if (2 == app->getProtocolVersion())
                {
                    NsSmartDeviceLinkRPCV2::OnTBTClientState* onTBTClientState = new NsSmartDeviceLinkRPCV2::OnTBTClientState;
                    onTBTClientState->set_state(notification-> get_state());
                    onTBTClientState->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnTBTClientStateID);
                    onTBTClientState->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                    MobileHandler::getInstance().sendRPCMessage(onTBTClientState, app->getAppID());
                }
                else
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "This command is available in protocol V2 only!");
                }
                break;
            }
            default:
                LOG4CPLUS_INFO_EXT(mLogger, " Not UI RPC message " << msg->getMethod() << " has been received!");
        }

        switch(msg->getMethod())
        {
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_VR__GETSUPPORTEDLANGUAGESRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "Get Supported Languages for UI response is received.");
                NsRPC2Communication::VR::GetSupportedLanguagesResponse * languages =
                    static_cast<NsRPC2Communication::VR::GetSupportedLanguagesResponse*>(msg);
                if (NsSmartDeviceLinkRPCV2::Result::SUCCESS == languages->getResult())
                {
                    core->mVRSupportedLanguages = languages->get_languages();
                }

                --core->mHMIStartupFlag;
                if (!core->mHMIStartupFlag)
                {
                    ConnectionHandler::getInstance().startTransportManager();
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_VR__GETCAPABILITIESRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A GetVRCapabilities response has been income");
                NsRPC2Communication::VR::GetCapabilitiesResponse * vrCaps =
                    (NsRPC2Communication::VR::GetCapabilitiesResponse*)msg;
                core->mVrCapabilitiesV2.set(vrCaps->get_capabilities());

                --core->mHMIStartupFlag;
                if (!core->mHMIStartupFlag)
                {
                    ConnectionHandler::getInstance().startTransportManager();
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_VR__GETLANGUAGERESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "GetLanguageResponse from VR is received");
                NsRPC2Communication::VR::GetLanguageResponse* getLang = (NsRPC2Communication::VR::GetLanguageResponse*)msg;
                core->mVrLanguageV2 = getLang->get_language();
                NsSmartDeviceLinkRPC::Language langV1;
                langV1.set((NsSmartDeviceLinkRPC::Language::LanguageInternal)getLang->get_language().get());
                core->mVrLanguageV1 = langV1;

                --core->mHMIStartupFlag;
                if (!core->mHMIStartupFlag)
                {
                    ConnectionHandler::getInstance().startTransportManager();
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_VR__ADDCOMMANDRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " An AddCommand VR response has been income");
                NsRPC2Communication::VR::AddCommandResponse* object = (NsRPC2Communication::VR::AddCommandResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                /*int appId = app->getAppID();
                unsigned int cmdId = core->mRequestMapping.findRequestIdAssignedToMessage(object->getId());
                app->decrementUnrespondedRequestCount(cmdId);*/
                unsigned int correlationID = it->second->correlationID;
                int connectionKey = it->second->connectionKey;
                if(core->decreaseMessageChain(it))
                {
                    switch(app->getProtocolVersion())
                    {
                        case 1:
                        {
                            NsSmartDeviceLinkRPC::AddCommand_response* response = new NsSmartDeviceLinkRPC::AddCommand_response();
                            response->set_success(true);
                            response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                            response->setCorrelationID(correlationID);
                            LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName());
                            MobileHandler::getInstance().sendRPCMessage(response, connectionKey);
                            break;
                        }
                        case 2:
                        {
                            NsSmartDeviceLinkRPCV2::AddCommand_response* response = new NsSmartDeviceLinkRPCV2::AddCommand_response();
                            response->set_success(true);
                            response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                            response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::AddCommandID);
                            response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                            response->setCorrelationID(correlationID);
                            LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName());
                            MobileHandler::getInstance().sendRPCMessage(response, connectionKey);
                            break;
                        }
                    }
                }

                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_VR__DELETECOMMANDRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A DeleteCommand VR response has been income");
                NsRPC2Communication::VR::DeleteCommandResponse* object = (NsRPC2Communication::VR::DeleteCommandResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                /*int appId = app->getAppID();
                unsigned int cmdId = core->mRequestMapping.findRequestIdAssignedToMessage(object->getId());
                app->decrementUnrespondedRequestCount(cmdId);*/
                int connectionKey = it->second->connectionKey;
                unsigned int correlationID = it->second->correlationID;
                if(core->decreaseMessageChain(it))
                {
                    switch(app->getProtocolVersion())
                    {
                        case 1:
                        {
                            NsSmartDeviceLinkRPC::DeleteCommand_response* response = new NsSmartDeviceLinkRPC::DeleteCommand_response();
                            // TODO(PV): use here result from message chain.
                            response->set_success(true);
                            response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                            response->setCorrelationID(correlationID);
                            LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName());
                            MobileHandler::getInstance().sendRPCMessage(response, connectionKey);
                            break;
                        }
                        case 2:
                        {
                            NsSmartDeviceLinkRPCV2::DeleteCommand_response* response = new NsSmartDeviceLinkRPCV2::DeleteCommand_response();
                            response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::DeleteCommandID);
                            response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                            // TODO(PV): use here result from message chain.
                            response->set_success(true);
                            response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                            response->setCorrelationID(correlationID);
                            LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName());
                            MobileHandler::getInstance().sendRPCMessage(response, connectionKey);
                            break;
                        }
                    }
                }

                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_VR__ONCOMMAND:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " An OnCommand VR notification has been invoked");
                NsRPC2Communication::VR::OnCommand* object = (NsRPC2Communication::VR::OnCommand*)msg;
                Application* app = core->getApplicationByCommand(object->get_cmdID(), object->get_appId());
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::OnCommand* event = new NsSmartDeviceLinkRPC::OnCommand();
                        event->set_cmdID(object->get_cmdID());
                        event->set_triggerSource(NsSmartDeviceLinkRPC::TriggerSource::TS_VR);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(event, appId);
                        break;
                    }
                    case 2:
                    {
                        NsSmartDeviceLinkRPCV2::OnCommand* event = new NsSmartDeviceLinkRPCV2::OnCommand();
                        event->set_cmdID(object->get_cmdID());
                        event->set_triggerSource(NsSmartDeviceLinkRPCV2::TriggerSource::TS_VR);
                        event->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnCommandID);
                        event->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << appId);
                        MobileHandler::getInstance().sendRPCMessage(event, appId);
                        break;
                    }
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_VR__ONLANGUAGECHANGE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "VR::OnLanguageChange is received from HMI.");
                NsRPC2Communication::VR::OnLanguageChange * languageChange =
                    static_cast<NsRPC2Communication::VR::OnLanguageChange*>(msg);
                if ( languageChange->get_language().get() != core->mVrLanguageV2.get() )
                {
                    // TODO(PV): clear mess around versions up.
                    core->mVrLanguageV2 = languageChange->get_language();
                    core->mVrLanguageV1.set(static_cast<NsSmartDeviceLinkRPC::Language::LanguageInternal>(languageChange->get_language().get()));

                    for(std::map<int, Application*>::const_iterator it = core->mApplications.begin();
                            it != core->mApplications.end();
                            ++it)
                    {
                        if ( 0 != it->second &&
                                1 != it->second->getProtocolVersion() )
                        {
                            NsSmartDeviceLinkRPCV2::OnLanguageChange * languageChangeToApp =
                                new NsSmartDeviceLinkRPCV2::OnLanguageChange;
                            languageChangeToApp->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                            languageChangeToApp->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnLanguageChangeID);
                            languageChangeToApp->set_hmiDisplayLanguage(core->mUiLanguageV2);
                            languageChangeToApp->set_language(core->mVrLanguageV2);
                            MobileHandler::getInstance().sendRPCMessage(languageChangeToApp, it->first);
                        }
                    }
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_VR__CHANGEREGISTRATIONRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "VR::ChangeRegistrationResponse is received from HMI.");
                NsRPC2Communication::VR::ChangeRegistrationResponse * response =
                    static_cast<NsRPC2Communication::VR::ChangeRegistrationResponse*>(msg);
                MessageChains::iterator it = core->mMessageChaining.find(response->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << response->getId());
                    return;
                }
                Application_v2* app = static_cast<Application_v2*>(core->getItem(it->second->connectionKey));
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                // TODO(PV): exchange when result is not succes.
                int connectionKey = it->second->connectionKey;
                unsigned int correlationID = it->second->correlationID;

                if(core->decreaseMessageChain(it))
                {
                    if ( NsSmartDeviceLinkRPCV2::Result::SUCCESS != response->getResult() )
                    {
                        sendResponse<NsSmartDeviceLinkRPCV2::ChangeRegistration_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                                NsSmartDeviceLinkRPCV2::FunctionID::ChangeRegistrationID
                                , static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(response->getResult())
                                , correlationID
                                , false
                                , connectionKey);
                        // TODO(PV): not sure if this is correct behaviour
                        app->setLanguageDesired(core->mVrLanguageV2);
                    }
                    else
                    {
                        sendResponse<NsSmartDeviceLinkRPCV2::ChangeRegistration_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                                NsSmartDeviceLinkRPCV2::FunctionID::ChangeRegistrationID
                                , static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(response->getResult())
                                , correlationID
                                , true
                                , connectionKey);
                    }
                    LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                        << " application id " << connectionKey);
                }

                return;
            }
            default:
                LOG4CPLUS_INFO_EXT(mLogger, " Not VR RPC message " << msg->getMethod() << " has been received!");
        }

        switch(msg->getMethod())
        {
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_TTS__GETSUPPORTEDLANGUAGESRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "Get Supported Languages for UI response is received.");
                NsRPC2Communication::TTS::GetSupportedLanguagesResponse * languages =
                    static_cast<NsRPC2Communication::TTS::GetSupportedLanguagesResponse*>(msg);
                if (NsSmartDeviceLinkRPCV2::Result::SUCCESS == languages->getResult())
                {
                    core->mTTSSupportedLanguages = languages->get_languages();
                }

                --core->mHMIStartupFlag;
                if (!core->mHMIStartupFlag)
                {
                    ConnectionHandler::getInstance().startTransportManager();
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_TTS__GETCAPABILITIESRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A GetTTSCapabilities response has been income");
                NsRPC2Communication::TTS::GetCapabilitiesResponse * ttsCaps = (NsRPC2Communication::TTS::GetCapabilitiesResponse*)msg;
                core->mSpeechCapabilitiesV2.set(ttsCaps->get_capabilities());

                --core->mHMIStartupFlag;
                if (!core->mHMIStartupFlag)
                {
                    ConnectionHandler::getInstance().startTransportManager();
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_TTS__GETLANGUAGERESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "GetLanguage Response from TTS is received.");
                NsRPC2Communication::TTS::GetLanguageResponse* getLang = (NsRPC2Communication::TTS::GetLanguageResponse*)msg;
                core->mTtsLanguageV2 = getLang->get_language();

                --core->mHMIStartupFlag;
                if (!core->mHMIStartupFlag)
                {
                    ConnectionHandler::getInstance().startTransportManager();
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_TTS__SPEAKRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A Speak response has been income");
                NsRPC2Communication::TTS::SpeakResponse* object = (NsRPC2Communication::TTS::SpeakResponse*)msg;
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::Speak_response* response = new NsSmartDeviceLinkRPC::Speak_response();
                        //response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        NsSmartDeviceLinkRPCV2::Result::SUCCESS == static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult())
                                    ? response->set_success(true) : response->set_success(false);
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName());
                        MobileHandler::getInstance().sendRPCMessage(response, it->second->connectionKey);
                        break;
                    }
                    case 2:
                    {
                        NsSmartDeviceLinkRPCV2::Speak_response* response = new NsSmartDeviceLinkRPCV2::Speak_response();
                        response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                        response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::SpeakID);
                        response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                        NsSmartDeviceLinkRPCV2::Result::SUCCESS == static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult())
                                    ? response->set_success(true) : response->set_success(false);
                        response->setCorrelationID(it->second->correlationID);
                        LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                            << " application id " << it->second->connectionKey);
                        MobileHandler::getInstance().sendRPCMessage(response, it->second->connectionKey);
                        break;
                    }
                }
                core->decreaseMessageChain(it);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_TTS__ONLANGUAGECHANGE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "TTS::OnLanguageChange is received from HMI.");
                NsRPC2Communication::TTS::OnLanguageChange * languageChange =
                    static_cast<NsRPC2Communication::TTS::OnLanguageChange*>(msg);
                if ( languageChange->get_language().get() != core->mTtsLanguageV2.get() )
                {
                    // TODO(PV): clear mess around versions up.
                    core->mTtsLanguageV2 = languageChange->get_language();
                    core->mTtsLanguageV1.set(static_cast<NsSmartDeviceLinkRPC::Language::LanguageInternal>(languageChange->get_language().get()));

                    for( std::map<int, Application*>::const_iterator it = core->mApplications.begin();
                            it != core->mApplications.end();
                            ++it )
                    {
                        if ( 0 != it->second &&
                                1 != it->second->getProtocolVersion() )
                        {
                            NsSmartDeviceLinkRPCV2::OnLanguageChange * languageChangeToApp =
                                new NsSmartDeviceLinkRPCV2::OnLanguageChange;
                            languageChangeToApp->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                            languageChangeToApp->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnLanguageChangeID);
                            languageChangeToApp->set_hmiDisplayLanguage(core->mUiLanguageV2);
                            languageChangeToApp->set_language(core->mTtsLanguageV2);
                            MobileHandler::getInstance().sendRPCMessage(languageChangeToApp, it->first);
                        }
                    }
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_TTS__CHANGEREGISTRATIONRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "TTS::ChangeRegistrationResponse is received from HMI.");
                NsRPC2Communication::TTS::ChangeRegistrationResponse * response =
                    static_cast<NsRPC2Communication::TTS::ChangeRegistrationResponse*>(msg);
                MessageChains::iterator it = core->mMessageChaining.find(response->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << response->getId());
                    return;
                }
                Application_v2* app = static_cast<Application_v2*>(core->getItem(it->second->connectionKey));
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int connectionKey = it->second->connectionKey;
                unsigned int correlationID = it->second->correlationID;

                if(core->decreaseMessageChain(it))
                {
                    if ( NsSmartDeviceLinkRPCV2::Result::SUCCESS != response->getResult() )
                    {
                        sendResponse<NsSmartDeviceLinkRPCV2::ChangeRegistration_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                                NsSmartDeviceLinkRPCV2::FunctionID::ChangeRegistrationID
                                , static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(response->getResult())
                                , correlationID
                                , false
                                , connectionKey);
                        // TODO(PV): not sure if this is correct behaviour
                        app->setLanguageDesired(core->mVrLanguageV2);
                    }
                    else
                    {
                        sendResponse<NsSmartDeviceLinkRPCV2::ChangeRegistration_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                                NsSmartDeviceLinkRPCV2::FunctionID::ChangeRegistrationID
                                , static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(response->getResult())
                                , correlationID
                                , true
                                , connectionKey);
                    }
                    LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                        << " application id " << connectionKey);
                }

                return;
            }
            default:
                LOG4CPLUS_INFO_EXT(mLogger, " Not TTS RPC message " << msg->getMethod() << " has been received!");
        }

        switch(msg->getMethod())
        {
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_BASICCOMMUNICATION__ACTIVATEAPP:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "ActivateApp has been received!");
                NsRPC2Communication::BasicCommunication::ActivateApp* object = static_cast<NsRPC2Communication::BasicCommunication::ActivateApp*>(msg);
                if ( !object )
                {
                    LOG4CPLUS_WARN(mLogger, "Couldn't cast object to ActivateApp type");
                    sendResponse<NsRPC2Communication::BasicCommunication::ActivateAppResponse>(
                        object->getId(), NsSmartDeviceLinkRPCV2::Result::GENERIC_ERROR);
                    return;
                }

                Application * app = core->getItem(object->get_appId());
                if ( !app )
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application with id " << object->get_appId() << " found!");
                    sendResponse<NsRPC2Communication::BasicCommunication::ActivateAppResponse>(
                        object->getId(), NsSmartDeviceLinkRPCV2::Result::INVALID_DATA);
                    return;
                }

                if ( !core->performActivitiesForActivatingApp(app) )
                {
                    sendResponse<NsRPC2Communication::BasicCommunication::ActivateAppResponse>(
                        object->getId(), NsSmartDeviceLinkRPCV2::Result::GENERIC_ERROR);
                }
                else
                {
                    sendResponse<NsRPC2Communication::BasicCommunication::ActivateAppResponse>(
                        object->getId(), NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                }

                LOG4CPLUS_INFO_EXT(mLogger, "New app  " << app->getName() << " id " << app->getAppID() << " activated!");
                return;
            }
            /*case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_SmartDeviceLinkCORE__DEACTIVATEAPP:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "DeactivateApp has been received!");
                NsRPC2Communication::BasicCommunication::DeactivateApp* object = static_cast<NsRPC2Communication::BasicCommunication::DeactivateApp*>(msg);
                if ( !object )
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "Couldn't cast object to DeactivateApp type");
                    return;
                }

                Application* currentApp = AppMgrRegistry::getInstance().getActiveItem();
                if (!currentApp)
                {
                    LOG4CPLUS_INFO_EXT(mLogger, "No application is currently active");
                    return;
                }

                //switch(currentApp->getApplicationHMIStatusLevel())
                //{
                //    case NsSmartDeviceLinkRPC::HMILevel::HMI_FULL:
                //    break;
                //    case NsSmartDeviceLinkRPC::HMILevel::HMI_LIMITED:
                //    break;
                //    case NsSmartDeviceLinkRPC::HMILevel::HMI_BACKGROUND:
                //    break;
                //   case NsSmartDeviceLinkRPC::HMILevel::HMI_NONE:
                //    break;
                //}

                break;
            }*/
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_BASICCOMMUNICATION__SENDDATA:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "SendData request has been received!");
                NsRPC2Communication::BasicCommunication::SendData* object = static_cast<NsRPC2Communication::BasicCommunication::SendData*>(msg);
                core->mSyncPManager.setRawData( object->get_data() );
                Application* app = core->getActiveItem();
                if(!app)
                {
                    LOG4CPLUS_WARN(mLogger, " No active application found!");
                    return;
                }

                int appId = app->getAppID();

                switch(app->getProtocolVersion())
                {
                    case 1:
                    {
                        NsSmartDeviceLinkRPC::OnEncodedSyncPData* encodedNotification = new NsSmartDeviceLinkRPC::OnEncodedSyncPData;
                        encodedNotification->set_data(core->mSyncPManager.getPData());
                        MobileHandler::getInstance().sendRPCMessage( encodedNotification, appId );
                        NsRPC2Communication::BasicCommunication::SendDataResponse* response = new NsRPC2Communication::BasicCommunication::SendDataResponse;
                        response->setId(object->getId());
                        response->setResult(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                        HMIHandler::getInstance().sendResponse(response);
                        break;
                    }
                    case 2:
                    {
                        NsRPC2Communication::BasicCommunication::SendDataResponse* response = new NsRPC2Communication::BasicCommunication::SendDataResponse;
                        response->setId(object->getId());
                        const std::string* urlPtr = object->get_url();
                        const int* timeoutPtr = object->get_timeout();
                        if(urlPtr)
                        {
                            const std::string& url = *urlPtr;
                            const int& timeout = timeoutPtr ? *timeoutPtr : 0;
                            LOG4CPLUS_INFO_EXT(mLogger, "SendData about to send at " << url << " timeout " << timeout);
                            thread_data* data = new thread_data;
                            data->pdata = core->mSyncPManager.getPData();
                            data->timeout = timeout;
                            data->url = url;
                            int rc = pthread_create(&sendingThread, 0, SendPData,
                                           (void *) data);
                            if (rc)
                            {
                                 LOG4CPLUS_ERROR_EXT(mLogger, "Couldn't start a thread: return code from pthread_create() is " << rc);
                                 response->setResult(NsSmartDeviceLinkRPCV2::Result::GENERIC_ERROR);
                                 HMIHandler::getInstance().sendResponse(response);
                                 return;
                            }
                            LOG4CPLUS_INFO_EXT(mLogger, "Data sending thread started!");
                        }
                        else
                        {
                            NsSmartDeviceLinkRPCV2::OnEncodedSyncPData* encodedNotification = new NsSmartDeviceLinkRPCV2::OnEncodedSyncPData;
                            encodedNotification->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnEncodedSyncPDataID);
                            encodedNotification->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                            encodedNotification->set_data(core->mSyncPManager.getPData());
                            MobileHandler::getInstance().sendRPCMessage( encodedNotification, appId );
                        }
                        response->setResult(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                        HMIHandler::getInstance().sendResponse(response);
                        break;
                    }
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_BASICCOMMUNICATION__GETAPPLIST:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "GetAppList request has been received!");
                NsRPC2Communication::BasicCommunication::GetAppList* object = static_cast<NsRPC2Communication::BasicCommunication::GetAppList*>(msg);
                NsRPC2Communication::BasicCommunication::GetAppListResponse* response = new NsRPC2Communication::BasicCommunication::GetAppListResponse;
                response->setId(object->getId());
                std::vector< NsSmartDeviceLinkRPCV2::HMIApplication> hmiApps;

                for(std::map<int,Application*>::const_iterator it = core->mApplications.begin();
                        it != core->mApplications.end();
                        ++ it)
                {
                    NsSmartDeviceLinkRPCV2::HMIApplication hmiApp;
                    Application* app = it->second;
                    if(!app)
                    {
                        LOG4CPLUS_WARN(mLogger, " null-application found!");
                        continue;
                    }
                    if(app->getAppID() != it->first)
                    {
                        continue;
                    }

                    LOG4CPLUS_INFO_EXT(mLogger, "Adding an application " << app->getName()
                        << " application id " << app->getAppID()
                        << " is media? " << app->getIsMediaApplication() );

                    /*const NsConnectionHandler::tDeviceHandle& deviceHandle = core->mDeviceHandler.findDeviceAssignedToSession(app->getAppID());
                    const NsConnectionHandler::CDevice* device = core->mDeviceList.findDeviceByHandle(deviceHandle);
                    if(!device)
                    {
                        LOG4CPLUS_ERROR_EXT(mLogger, " Cannot retreive current device name for the message with app ID " << app->getAppID() << " !");
                        return;
                    }*/
                    //const std::string& deviceName = "DEVICE";//device->getUserFriendlyName();

                    hmiApp.set_appName(app->getName());
                    hmiApp.set_appId(app->getAppID());
                    hmiApp.set_isMediaApplication(app->getIsMediaApplication());

                    std::map<int,DeviceStorage>::const_iterator dev = core->mDevices.find( app->getDeviceHandle() );
                    std::string deviceName = "";
                    if ( core->mDevices.end() != dev )
                    {
                        deviceName = dev->second.getUserFriendlyName();
                    }

                    hmiApp.set_deviceName(deviceName);

                    if ( 1 == app->getProtocolVersion() )
                    {
                        Application_v1 * app1 = static_cast<Application_v1*>(app);
                        hmiApp.set_hmiDisplayLanguageDesired(static_cast<NsSmartDeviceLinkRPCV2::Language::LanguageInternal>(app1->getLanguageDesired().get()));
                        hmiApp.set_languageDesired(static_cast<NsSmartDeviceLinkRPCV2::Language::LanguageInternal>(app1->getLanguageDesired().get()));
                    }
                    if ( 2 == app->getProtocolVersion() )
                    {
                        Application_v2 * app2 = static_cast<Application_v2*>(app);
                        hmiApp.set_hmiDisplayLanguageDesired(app2->getHMIDisplayLanguageDesired());
                        hmiApp.set_languageDesired(app2->getLanguageDesired());
                        hmiApp.set_appType(app2->getAppType());
                    }

                    LOG4CPLUS_INFO_EXT(mLogger, "Added an application " << hmiApp.get_appName()
                        << " application id " << hmiApp.get_appId()
                        << " is media? " << hmiApp.get_isMediaApplication() );
                    hmiApps.push_back(hmiApp);
                }
                if(!hmiApps.empty())
                {
                    response->set_appList(hmiApps);
                    response->setResult(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                }
                else
                {
                    LOG4CPLUS_WARN(mLogger, " Application list is empty!");
                    response->setResult(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                }

                Json::Value commandJson = NsRPC2Communication::Marshaller::toJSON( response );
                LOG4CPLUS_INFO(mLogger, "JSONRPC2Handler::waitForCommandsToHMI: received command text: " << commandJson);
                HMIHandler::getInstance().sendResponse(response);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_BASICCOMMUNICATION__GETDEVICELIST:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A GetDeviceList request has been income");
                NsRPC2Communication::BasicCommunication::GetDeviceList* getDevList = (NsRPC2Communication::BasicCommunication::GetDeviceList*)msg;
                NsRPC2Communication::BasicCommunication::GetDeviceListResponse* response = new NsRPC2Communication::BasicCommunication::GetDeviceListResponse;
                response->setId(getDevList->getId());
                response->setResult(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                ConnectionHandler::getInstance().startDevicesDiscovery();
                HMIHandler::getInstance().sendResponse(response);
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_BASICCOMMUNICATION__ONAPPDEACTIVATED:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "OnAppDeactivated has been received.");
                NsRPC2Communication::BasicCommunication::OnAppDeactivated * appDeacivated =
                    static_cast<NsRPC2Communication::BasicCommunication::OnAppDeactivated*>(msg);

                Application* currentApp = core->getActiveItem();
                if ( !currentApp )
                {
                    LOG4CPLUS_INFO(mLogger, "No application is currently acitve.");
                    return;
                }
                if ( currentApp->getAppID() != appDeacivated->get_appId() )
                {
                    LOG4CPLUS_WARN(mLogger, "Currently active application is not the same as on HMI.");
                    return;
                }

                // TODO(PV): don't forget after refactoring: get to this switch only if not in NONE
                switch ( appDeacivated->get_reason().get() )
                {
                    case NsSmartDeviceLinkRPCV2::DeactivateReason::AUDIO:
                    case NsSmartDeviceLinkRPCV2::DeactivateReason::PHONECALL:
                    {
                        if ( currentApp->getIsMediaApplication() )
                        {
                            currentApp->setApplicationAudioStreamingState(
                                NsSmartDeviceLinkRPCV2::AudioStreamingState::NOT_AUDIBLE);
                        }
                        currentApp->setApplicationHMIStatusLevel(
                            NsSmartDeviceLinkRPCV2::HMILevel::HMI_BACKGROUND);
                        break;

                    }
                    case NsSmartDeviceLinkRPCV2::DeactivateReason::NAVIGATIONMAP:
                    case NsSmartDeviceLinkRPCV2::DeactivateReason::PHONEMENU:
                    case NsSmartDeviceLinkRPCV2::DeactivateReason::SYNCSETTINGS:
                    case NsSmartDeviceLinkRPCV2::DeactivateReason::GENERAL:
                    {
                        if ( currentApp->getIsMediaApplication() )
                        {
                            // TODO(PV): for media it seems for FULL it is neccessarily that its audio is AUDIBLE.
                            if ( NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL ==
                                currentApp->getApplicationHMIStatusLevel() )
                            {
                                currentApp->setApplicationHMIStatusLevel(
                                    NsSmartDeviceLinkRPCV2::HMILevel::HMI_LIMITED);
                            }
                        }
                        else
                        {
                            currentApp->setApplicationHMIStatusLevel(
                                NsSmartDeviceLinkRPCV2::HMILevel::HMI_BACKGROUND);
                        }
                        break;
                    }
                    default:
                    {
                        // TODO(PV): what should be done in this case?
                        return;
                    }
                }

                core->sendHMINotificationToMobile( currentApp );
                return;
            }
            default:
                LOG4CPLUS_INFO_EXT(mLogger, " Not SmartDeviceLinkCore RPC message " << msg->getMethod() << " has been received!");
        }

        switch(msg->getMethod())
        {
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_VEHICLEINFO__GETVEHICLETYPERESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A GetVehicleType response has been income");
                NsRPC2Communication::VehicleInfo::GetVehicleTypeResponse* getVehType = (NsRPC2Communication::VehicleInfo::GetVehicleTypeResponse*)msg;
                core->mVehicleType = getVehType->get_vehicleType();

                --core->mHMIStartupFlag;
                if (!core->mHMIStartupFlag)
                {
                    ConnectionHandler::getInstance().startTransportManager();
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_VEHICLEINFO__GETVEHICLEDATARESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A GetVehicleData response has been income");
                NsRPC2Communication::VehicleInfo::GetVehicleDataResponse* object = static_cast<NsRPC2Communication::VehicleInfo::GetVehicleDataResponse*>(msg);
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                int appId = app->getAppID();

                if (2 == app->getProtocolVersion())
                {
                    NsSmartDeviceLinkRPCV2::GetVehicleData_response* response = new NsSmartDeviceLinkRPCV2::GetVehicleData_response();
                    response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::GetVehicleDataID);
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->set_resultCode(NsSmartDeviceLinkRPCV2::Result::SUCCESS);
                    response->set_success(true);
                    if (object->get_gps())
                    {
                        response->set_gps(*(object->get_gps()));
                    }
                    if (object->get_speed())
                    {
                        response->set_speed(*(object->get_speed()));
                    }
                    if (object->get_rpm())
                    {
                        response->set_rpm(*(object->get_rpm()));
                    }
                    if (object->get_fuelLevel())
                    {
                        response->set_fuelLevel(*(object->get_fuelLevel()));
                    }
                    if (object->get_avgFuelEconomy())
                    {
                        response->set_avgFuelEconomy(*(object->get_avgFuelEconomy()));
                    }
                    if (object->get_batteryVoltage())
                    {
                        response->set_batteryVoltage(*(object->get_batteryVoltage()));
                    }
                    if (object->get_externalTemperature())
                    {
                        response->set_externalTemperature(*(object->get_externalTemperature()));
                    }
                    if (object->get_vin())
                    {
                        response->set_vin(*(object->get_vin()));
                    }
                    if (object->get_prndl())
                    {
                        response->set_prndl(*(object->get_prndl()));
                    }
                    if (object->get_tirePressure())
                    {
                        response->set_tirePressure(*(object->get_tirePressure()));
                    }
                    if (object->get_batteryPackVoltage())
                    {
                        response->set_batteryPackVoltage(*(object->get_batteryPackVoltage()));
                    }
                    if (object->get_batteryPackCurrent())
                    {
                        response->set_batteryPackCurrent(*(object->get_batteryPackCurrent()));
                    }
                    if (object->get_batteryPackTemperature())
                    {
                        response->set_batteryPackTemperature(*(object->get_batteryPackTemperature()));
                    }
                    if (object->get_engineTorque())
                    {
                        response->set_engineTorque(*(object->get_engineTorque()));
                    }
                    if (object->get_odometer())
                    {
                        response->set_odometer(*(object->get_odometer()));
                    }
                    if (object->get_tripOdometer())
                    {
                        response->set_tripOdometer(*(object->get_tripOdometer()));
                    }
                    if (object->get_satRadioESN())
                    {
                        response->set_satRadioESN(*(object->get_satRadioESN()));
                    }
                    response->setCorrelationID(it->second->correlationID);

                    LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName());
                    MobileHandler::getInstance().sendRPCMessage(response, it->second->connectionKey);
                    core->decreaseMessageChain(it);
                } else
                {
                    LOG4CPLUS_WARN(mLogger, "GetVehicleData is present in Protocol V2 only!!!");
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_VEHICLEINFO__ONVEHICLEDATA:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " An OnVehicleData notification has been income");
                NsRPC2Communication::VehicleInfo::OnVehicleData* object = static_cast<NsRPC2Communication::VehicleInfo::OnVehicleData*>(msg);
                if (object->get_gps())
                {
                }
                if (object->get_speed())
                {
                }
                if (object->get_rpm())
                {
                }
                if (object->get_fuelLevel())
                {
                }
                if (object->get_avgFuelEconomy())
                {
                }
                if (object->get_batteryVoltage())
                {
                }
                if (object->get_externalTemperature())
                {
                }
                if (object->get_vin())
                {
                }
                if (object->get_prndl())
                {
                    NsSmartDeviceLinkRPCV2::VehicleDataType vehicleDataName = NsSmartDeviceLinkRPCV2::VehicleDataType(NsSmartDeviceLinkRPCV2::VehicleDataType::VehicleDataTypeInternal::VEHICLEDATA_PRNDLSTATUS);
                    std::vector<Application*> result;
                    result.clear();
                    core->mVehicleDataMapping.findRegistryItemsSubscribedToVehicleData(vehicleDataName, result);
                    if (0 < result.size())
                    {
                        LOG4CPLUS_INFO_EXT(mLogger, " There are " << result.size() <<" subscribers on PRNDL notification!");
                        for (std::vector<Application*>::iterator it = result.begin(); it != result.end(); it++)
                        {
                            Application_v2* app = (Application_v2*)(*it);
                            if(!app)
                            {
                                LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with the registry item" );
                                continue;
                            }
                            int appId = app->getAppID();
                            LOG4CPLUS_INFO_EXT(mLogger, " An OnVehicleData PRNDL notification sending to " << appId);
                            NsSmartDeviceLinkRPCV2::OnVehicleData* notification = new NsSmartDeviceLinkRPCV2::OnVehicleData();
                            notification->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnVehicleDataID);
                            notification->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                            notification->set_prndl(*(object->get_prndl()));
                            LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                                                        << " application id " << appId);
                            MobileHandler::getInstance().sendRPCMessage(notification, appId);
                        }
                    } else
                    {
                        LOG4CPLUS_INFO_EXT(mLogger, " No subscribers found!");
                    }
                }
                if (object->get_tirePressure())
                {
                }
                if (object->get_batteryPackVoltage())
                {
                }
                if (object->get_batteryPackCurrent())
                {
                }
                if (object->get_batteryPackTemperature())
                {
                }
                if (object->get_engineTorque())
                {
                }
                if (object->get_odometer())
                {
                }
                if (object->get_tripOdometer())
                {
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_VEHICLEINFO__GETDTCSRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, " A GetDTCs response has been income");
                NsRPC2Communication::VehicleInfo::GetDTCsResponse* object = static_cast<NsRPC2Communication::VehicleInfo::GetDTCsResponse*>(msg);
                MessageChains::iterator it = core->mMessageChaining.find(object->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << object->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                if (2 == app->getProtocolVersion())
                {
                    NsSmartDeviceLinkRPCV2::GetDTCs_response* response = new NsSmartDeviceLinkRPCV2::GetDTCs_response();
                    response->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::GetDTCsID);
                    response->setCorrelationID(it->second->correlationID);
                    response->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    response->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult()));
                    NsSmartDeviceLinkRPCV2::Result::SUCCESS == static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(object->getResult())
                                ? response->set_success(true) : response->set_success(false);
                    response->setCorrelationID(it->second->correlationID);
                    if (object->get_dtcList())
                    {
                        LOG4CPLUS_INFO_EXT(mLogger, " dtcList is present! ");
                        response->set_dtcList(*(object->get_dtcList()));
                    }

                    LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                        << " application id " << it->second->connectionKey);
                    MobileHandler::getInstance().sendRPCMessage(response, it->second->connectionKey);
                    core->decreaseMessageChain(it);
                } else
                {
                    LOG4CPLUS_WARN(mLogger, "GetVehicleData is present in Protocol V2 only!!!");
                }
                return;
            }
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_VEHICLEINFO__READDIDRESPONSE:
            {
                LOG4CPLUS_INFO_EXT(mLogger, "ReadDID response is received from HMI.");
                NsRPC2Communication::VehicleInfo::ReadDIDResponse * response =
                    static_cast<NsRPC2Communication::VehicleInfo::ReadDIDResponse*>(msg);
                MessageChains::iterator it = core->mMessageChaining.find(response->getId());
                if ( core->mMessageChaining.end() == it )
                {
                    LOG4CPLUS_WARN_EXT(mLogger, "No chain for " << response->getId());
                    return;
                }
                Application* app = core->getItem(it->second->connectionKey);
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    return;
                }

                if (2 == app->getProtocolVersion())
                {
                    NsSmartDeviceLinkRPCV2::ReadDID_response * readDIDResponse = new NsSmartDeviceLinkRPCV2::ReadDID_response;
                    readDIDResponse->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::ReadDIDID);
                    readDIDResponse->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE);
                    bool isSuccess = true;
                    if ( NsSmartDeviceLinkRPCV2::Result::SUCCESS != response->getResult() &&
                            NsSmartDeviceLinkRPCV2::Result::ENCRYPTED != response->getResult() )
                    {
                        isSuccess = false;
                    }
                    readDIDResponse->set_success(isSuccess);
                    readDIDResponse->set_resultCode(static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(response->getResult()));

                    if ( response->get_dataResult() )
                    {
                        readDIDResponse->set_dataResult( *response->get_dataResult() );
                    }

                    if ( response->get_data() )
                    {
                        readDIDResponse->set_data( *response->get_data() );
                    }
                    readDIDResponse->setCorrelationID(it->second->correlationID);
                    MobileHandler::getInstance().sendRPCMessage(readDIDResponse, it->second->connectionKey);
                } else
                {
                    LOG4CPLUS_WARN(mLogger, "ReadDID is present in Protocol V2 only!!!");
                }
                core->decreaseMessageChain(it);
                return;
            }
            default:
                LOG4CPLUS_INFO_EXT(mLogger, " Not VehicleInfo RPC message " << msg->getMethod() << " has been received!");
        }

        switch(msg->getMethod())
        {
            case NsRPC2Communication::Marshaller::METHOD_NSRPC2COMMUNICATION_UI__ALERTMANEUVERRESPONSE:
            {
                /*LOG4CPLUS_INFO_EXT(mLogger, "ReadDID response is received from HMI.");
                NsRPC2Communication::UI::AlertManeuverResponse * response =
                    static_cast<NsRPC2Communication::UI::AlertManeuverResponse*>(msg);
                Application* app = core->getApplicationFromItemCheckNotNull(
                        core->mMessageMapping.findRegistryItemAssignedToCommand(response->getId()));
                if(!app)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "No application associated with this registry item!");
                    sendResponse<NsSmartDeviceLinkRPCV2::AlertManeuver_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                        NsSmartDeviceLinkRPCV2::FunctionID::AlertManeuverID
                        , NsSmartDeviceLinkRPCV2::Result::APPLICATION_NOT_REGISTERED
                        , NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE
                        , false
                        , app->getAppID());
                    return;
                }

                sendResponse<NsSmartDeviceLinkRPCV2::AlertManeuver_response, NsSmartDeviceLinkRPCV2::Result::ResultInternal>(
                    NsSmartDeviceLinkRPCV2::FunctionID::AlertManeuverID
                    , static_cast<NsSmartDeviceLinkRPCV2::Result::ResultInternal>(response->getResult())
                    , NsSmartDeviceLinkRPC::SDLRPCMessage::RESPONSE
                    , NsSmartDeviceLinkRPCV2::Result::SUCCESS == response->getResult()
                    , app->getAppID());*/

                break;
            }
        }

        LOG4CPLUS_INFO_EXT(mLogger, " A RPC2 bus message " << msg->getMethod() << " has been invoked!");
    }

    /**
     * \brief Register an application
     * \param request a RegisterAppInterface request which is the source for application fields initial values
     * \param connectionID id of the connection which will be associated with the application
     * \param sessionID an id of the session which will be associated with the application
     * \return A instance of RegistryItem created for application
     */
    const Application* AppMgrCore::registerApplication(NsSmartDeviceLinkRPC::SDLRPCMessage * request, int appId)
    {
        LOG4CPLUS_INFO_EXT(mLogger, __PRETTY_FUNCTION__);
        if(!request)
        {
            LOG4CPLUS_ERROR_EXT(mLogger, "Null-request specified for application id " << appId << "!");
            return 0;
        }

        const unsigned int& protocolVersion = request->getProtocolVersion();
        std::string appName = "";

        switch(protocolVersion)
        {
            case 2:
            {
                appName = ((NsSmartDeviceLinkRPCV2::RegisterAppInterface_request*)request)->get_appName();
                Application_v2* application = new Application_v2(appName, appId);
                if(!application)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "Cannot register application " << appName
                        << " application id " << appId
                        << " protocol version " << protocolVersion
                        << " !");
                    return 0;
                }

                NsSmartDeviceLinkRPCV2::RegisterAppInterface_request* registerRequest = (NsSmartDeviceLinkRPCV2::RegisterAppInterface_request*) request;
       /*         if(registerRequest->get_appID())
                {
                    const std::string& appId = *registerRequest->get_appID();
                    application->setAppID(appId);
                }
        */        if( registerRequest->get_appType() )
                {
                    const std::vector<NsSmartDeviceLinkRPCV2::AppType>& appType = *registerRequest->get_appType();
                    application->setAppType(appType);
                }
                bool isMediaApplication = registerRequest->get_isMediaApplication();
                if (mVrLanguageV2.get() != mTtsLanguageV2.get())
                {
                    // TODO: posibly notify application that VR&TTS have different languages.
                    LOG4CPLUS_WARN(mLogger, "VR and TTS have different active languages. Unspecified behaviour.");
                }
                application->setLanguageDesired(mVrLanguageV2);

                const NsSmartDeviceLinkRPCV2::SyncMsgVersion& syncMsgVersion = registerRequest->get_syncMsgVersion();

                if ( registerRequest -> get_ngnMediaScreenAppName() )
                {
                    const std::string& ngnMediaScreenAppName = *registerRequest->get_ngnMediaScreenAppName();
                    application->setNgnMediaScreenAppName(ngnMediaScreenAppName);
                }

                if ( registerRequest -> get_vrSynonyms() )
                {
                    const std::vector<std::string>& vrSynonyms = *registerRequest->get_vrSynonyms();
                    application->setVrSynonyms(vrSynonyms);
                }

                application->setHMIDisplayLanguageDesired(mUiLanguageV2);
                application->setIsMediaApplication(isMediaApplication);
                application->setSyncMsgVersion(syncMsgVersion);
                application->setSystemContext(NsSmartDeviceLinkRPCV2::SystemContext::SYSCTXT_MENU);
                application->setApplicationAudioStreamingState(
                    static_cast<NsSmartDeviceLinkRPCV2::AudioStreamingState::AudioStreamingStateInternal>(1));//NsSmartDeviceLinkRPCV2::AudioStreamingState::NOT_AUDIBLE

                if(registerRequest->get_ttsName())
                {
                    application->setTtsName(*registerRequest->get_ttsName());
                }

                application->setApplicationHMIStatusLevel(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE);

                mApplications.insert( std::pair<int, Application*>(appId, application) );
                LOG4CPLUS_INFO_EXT(mLogger, "\n\t\t\t\tAdded application with appid " << appId <<
                        " to mApplications " << (int)mApplications[appId]);
                for( std::map<int, DeviceStorage>::iterator it = mDevices.begin();
                        it != mDevices.end();
                        ++it )
                {
                    if(it->second.getAppId() == appId)
                    {
                        application->setDeviceHandle(it->first);
                        LOG4CPLUS_INFO_EXT(mLogger, "\n\t\t\t\tSet for application " << appId << " device handle " << it->first );
                        for(std::set<int>::const_iterator connectionIt = it->second.getConnectionKeys().begin();
                                connectionIt != it->second.getConnectionKeys().end();
                                ++connectionIt)
                        {
                            mApplications.insert( std::pair<int, Application*>(*connectionIt, application) );
                        }
                        break;
                    }
                }

                return application;
            }
            case 1:
            {
                appName = ((NsSmartDeviceLinkRPC::RegisterAppInterface_request*)request)->get_appName();
                Application_v1* application = new Application_v1(appName, appId);
                if(!application)
                {
                    LOG4CPLUS_ERROR_EXT(mLogger, "Cannot register application " << appName
                        << " application id " << appId
                        << " protocol version " << protocolVersion
                        << " !");
                    return 0;
                }

                NsSmartDeviceLinkRPC::RegisterAppInterface_request* registerRequest = (NsSmartDeviceLinkRPC::RegisterAppInterface_request*) request;
                bool isMediaApplication = registerRequest->get_isMediaApplication();

                const NsSmartDeviceLinkRPC::SyncMsgVersion& syncMsgVersion = registerRequest->get_syncMsgVersion();

                if ( registerRequest -> get_ngnMediaScreenAppName() )
                {
                    const std::string& ngnMediaScreenAppName = *registerRequest->get_ngnMediaScreenAppName();
                    application->setNgnMediaScreenAppName(ngnMediaScreenAppName);
                }

                if ( registerRequest -> get_vrSynonyms() )
                {
                    const std::vector<std::string>& vrSynonyms = *registerRequest->get_vrSynonyms();
                    application->setVrSynonyms(vrSynonyms);
                }

                if ( registerRequest -> get_usesVehicleData() )
                {
                    bool usesVehicleData = registerRequest->get_usesVehicleData();
                    application->setUsesVehicleData(usesVehicleData);
                }

                application->setIsMediaApplication(isMediaApplication);
                application->setLanguageDesired(mUiLanguageV1);

                application->setSyncMsgVersion(syncMsgVersion);
                application->setSystemContext(NsSmartDeviceLinkRPCV2::SystemContext::SYSCTXT_MENU);
                application->setApplicationAudioStreamingState(NsSmartDeviceLinkRPCV2::AudioStreamingState::NOT_AUDIBLE);
                application->setApplicationHMIStatusLevel(NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE);

                LOG4CPLUS_INFO_EXT(mLogger, "Application created." );
                mApplications.insert( std::pair<int, Application*>(appId, application) );
                LOG4CPLUS_INFO_EXT(mLogger, "\n\t\t\t\tAdded application with appid " << appId <<
                        " to mApplications " << (int)mApplications[appId]);
                for( std::map<int, DeviceStorage>::iterator it = mDevices.begin();
                        it != mDevices.end();
                        ++it )
                {
                    if(it->second.getAppId() == appId)
                    {
                        application->setDeviceHandle(it->first);
                        LOG4CPLUS_INFO_EXT(mLogger, "\n\t\t\t\tSet for application " << appId << " device handle " << it->first );
                        for(std::set<int>::const_iterator connectionIt = it->second.getConnectionKeys().begin();
                                connectionIt != it->second.getConnectionKeys().end();
                                ++connectionIt)
                        {
                            mApplications.insert( std::pair<int, Application*>(*connectionIt, application) );
                        }
                        break;
                    }
                }
                return application;
            }
            default:
            {
                LOG4CPLUS_ERROR_EXT(mLogger, "Unsupported protocol version number " << protocolVersion << " !");
                return 0;
            }
        }
        LOG4CPLUS_INFO_EXT(mLogger, " Application " << appName << " application id " << appId << " registered successfully !");
        return 0;
    }

    /**
     * \brief unregister an application associated with the given session
     * \param appId an id of the application to be unregistered
     */
    void AppMgrCore::unregisterApplication(int appId)
    {
        LOG4CPLUS_INFO_EXT(mLogger, "Trying to unregister an application for application id " << appId);

        Application* app = getItem(appId);
        if(!app)
        {
            LOG4CPLUS_WARN_EXT(mLogger, "No application associated with this registry item!");
            return;
        }

        // Delete app files
        std::string fullPath = WorkWithOS::getFullPath(app->getName());
        std::string fullFilePath;
        LOG4CPLUS_INFO_EXT(mLogger, "Full path to app folder: " << fullPath);
        if (WorkWithOS::checkIfDirectoryExists(fullPath))
        {
            std::vector<std::string> files = WorkWithOS::listFilesInDirectory(fullPath);
            std::vector<std::string>::const_iterator i = files.begin();
            for (i; i != files.end(); ++i)
            {
                if (app->isPersistentFile(*i))
                {
                    continue;
                }

                fullFilePath = fullPath;
                fullFilePath += "/";
                fullFilePath += *i;
                LOG4CPLUS_INFO_EXT(mLogger, "File to be removed: " << fullFilePath);
                if (WorkWithOS::checkIfFileExists(fullFilePath))
                {
                    WorkWithOS::deleteFile(fullFilePath);
                }
            }

            WorkWithOS::deleteFile(fullPath);
        }

        const std::string& appName = app->getName();
        LOG4CPLUS_INFO_EXT(mLogger, " Unregistering an application " << appName
            << " application id " << appId
            << "!");

        mButtonsMapping.removeItem(app);
        //AppMgrRegistry::getInstance().unregisterApplication(item);

        LOG4CPLUS_INFO_EXT(mLogger, " Unregistered an application " << appName
            << " application id " << appId
            << "!");

        NsRPC2Communication::BasicCommunication::OnAppUnregistered* appUnregistered = new NsRPC2Communication::BasicCommunication::OnAppUnregistered();
        appUnregistered->set_appName(appName);
        appUnregistered->set_appId(app->getAppID());
        appUnregistered->set_reason(NsSmartDeviceLinkRPCV2::AppInterfaceUnregisteredReason(NsSmartDeviceLinkRPCV2::AppInterfaceUnregisteredReason::USER_EXIT));
        HMIHandler::getInstance().sendNotification(appUnregistered);
        mApplications.erase( appId );
        LOG4CPLUS_INFO_EXT(mLogger, "\n\t\t\t\tRemvoed application " << appId );
        std::map<int, DeviceStorage>::iterator it = mDevices.find( app->getDeviceHandle() );
        if (mDevices.end() != it)
        {
            it->second.setAppId(0);
            LOG4CPLUS_INFO_EXT(mLogger, "\n\t\t\t\tApplication " << appId << " removed from device " << it->first);
        }
    }

    void AppMgrCore::sendHMINotificationToMobile( Application * application )
    {
        if (1 == application->getProtocolVersion())
        {
            NsSmartDeviceLinkRPC::OnHMIStatus* hmiStatus = new NsSmartDeviceLinkRPC::OnHMIStatus;
            hmiStatus->set_audioStreamingState(
                static_cast<NsSmartDeviceLinkRPC::AudioStreamingState::AudioStreamingStateInternal>(
                    application->getApplicationAudioStreamingState().get()));
            hmiStatus->set_systemContext(
                static_cast<NsSmartDeviceLinkRPC::SystemContext::SystemContextInternal>(application->getSystemContext().get()));
            hmiStatus->set_hmiLevel(
                static_cast<NsSmartDeviceLinkRPC::HMILevel::HMILevelInternal>(application->getApplicationHMIStatusLevel()));
            MobileHandler::getInstance().sendRPCMessage(hmiStatus, application->getAppID());
        }
        else
        {
            NsSmartDeviceLinkRPCV2::OnHMIStatus* hmiStatus = new NsSmartDeviceLinkRPCV2::OnHMIStatus;
            hmiStatus->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
            hmiStatus->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnHMIStatusID);
            hmiStatus->set_audioStreamingState(application->getApplicationAudioStreamingState());
            hmiStatus->set_systemContext(application->getSystemContext());
            hmiStatus->set_hmiLevel(application->getApplicationHMIStatusLevel());
            MobileHandler::getInstance().sendRPCMessage(hmiStatus, application->getAppID());
        }
    }

    bool AppMgrCore::performActivitiesForActivatingApp( Application * app )
    {
        int appId = app->getAppID();

        Application* currentApp = getActiveItem();
        if (!currentApp)
        {
            LOG4CPLUS_INFO_EXT(mLogger, "No application is currently active");
        }
        else
        {
            if (currentApp->getAppID() == appId)
            {
                LOG4CPLUS_INFO_EXT(mLogger, "App is currently active");
                return false;
            }

            LOG4CPLUS_INFO_EXT(mLogger, "There is a currently active application  " << currentApp->getName()
                << " ID " << currentApp->getAppID()
                << " - about to remove it from HMI first");
            //removeAppFromHmi(currentApp, appId);
        }

        NsSmartDeviceLinkRPCV2::HMILevel::HMILevelInternal previousState = app->getApplicationHMIStatusLevel();

        if(!activateApp(app))
        {
            LOG4CPLUS_ERROR_EXT(mLogger, "Application " << app->getName()
                << " application id " << appId << " cannot be activated.");
            return false;
        }

        if ( NsSmartDeviceLinkRPCV2::HMILevel::HMI_NONE == previousState )
        {
            LOG4CPLUS_INFO_EXT(mLogger, "First time activating - sending stuff.");

            if ( 1== app->getProtocolVersion() )
            {
                if(mDriverDistractionV1)
                {
                    MobileHandler::getInstance().sendRPCMessage(mDriverDistractionV1, appId);
                }
            }
            else
            {
                Application_v2* appv2 = (Application_v2*)app;
                //Methods for changing language:
                NsRPC2Communication::VR::ChangeRegistration * changeVrRegistration =
                    new NsRPC2Communication::VR::ChangeRegistration;
                changeVrRegistration->set_language(appv2->getLanguageDesired());
                changeVrRegistration->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                changeVrRegistration->set_appId(appv2->getAppID());
                HMIHandler::getInstance().sendRequest(changeVrRegistration);

                NsRPC2Communication::TTS::ChangeRegistration * changeTtsRegistration =
                    new NsRPC2Communication::TTS::ChangeRegistration;
                changeTtsRegistration->set_language(appv2->getLanguageDesired());
                changeTtsRegistration->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                changeTtsRegistration->set_appId(appv2->getAppID());
                HMIHandler::getInstance().sendRequest(changeTtsRegistration);

                NsRPC2Communication::UI::ChangeRegistration * changeUIRegistration =
                    new NsRPC2Communication::UI::ChangeRegistration;
                changeUIRegistration->set_hmiDisplayLanguage(appv2->getHMIDisplayLanguageDesired());
                changeUIRegistration->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                changeUIRegistration->set_appId(appv2->getAppID());
                HMIHandler::getInstance().sendRequest(changeUIRegistration);
                //End of methods for languages.

                if(mDriverDistractionV2)
                {
                    MobileHandler::getInstance().sendRPCMessage(mDriverDistractionV2, appId);
                }
            }
        }

        return true;
    }

    /**
     * \brief Remove all app components from HMI
     * \param currentApp app which components to be removed
     * \param appId application id
     */
    /*void AppMgrCore::removeAppFromHmi(Application* currentApp, int appId)
    {
        const Commands& currentCommands = currentApp->getAllCommands();
        LOG4CPLUS_INFO_EXT(mLogger, "Removing current application's commands from HMI");

        for(Commands::const_iterator it = currentCommands.begin(); it != currentCommands.end(); it++)
        {
            const Command& key = *it;
            const CommandParams& params = key.second;
            const CommandBase& base = key.first;
            const CommandType& type = std::get<1>(base);
            unsigned int cmdId = std::get<0>(base);
            NsRPC2Communication::RPC2Request* deleteCmd = 0;
            if(type == CommandType::UI)
            {
                LOG4CPLUS_INFO_EXT(mLogger, "Removing UI command");
                deleteCmd = new NsRPC2Communication::UI::DeleteCommand();
            }
            else if(type == CommandType::VR)
            {
                LOG4CPLUS_INFO_EXT(mLogger, "Removing VR command");
                deleteCmd = new NsRPC2Communication::VR::DeleteCommand();
            }
            else
            {
                LOG4CPLUS_ERROR_EXT(mLogger, "An unindentified command type - " << type.getType());
                continue;
            }
            deleteCmd->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
            ((NsRPC2Communication::UI::DeleteCommand*)deleteCmd)->set_cmdId(cmdId); //doesn't matter, of which type- VR or UI is thye cmd = eather has the set_cmdId method within
            ((NsRPC2Communication::UI::DeleteCommand*)deleteCmd)->set_appId(currentApp->getAppID());
            mMessageMapping.addMessage(deleteCmd->getId(), appId);

            HMIHandler::getInstance().sendRequest(deleteCmd);
        }
        LOG4CPLUS_INFO_EXT(mLogger, "Current app's commands removed!");

        const MenuItems& currentMenus = currentApp->getAllMenus();
        LOG4CPLUS_INFO_EXT(mLogger, "Removing current application's menus from HMI");
        for(MenuItems::const_iterator it = currentMenus.begin(); it != currentMenus.end(); it++)
        {
            const unsigned int& menuId = it->first;
            NsRPC2Communication::UI::DeleteSubMenu* deleteCmd = new NsRPC2Communication::UI::DeleteSubMenu();
            deleteCmd->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
            deleteCmd->set_menuId(menuId);
            deleteCmd->set_appId(currentApp->getAppID());
            mMessageMapping.addMessage(deleteCmd->getId(), appId);

            HMIHandler::getInstance().sendRequest(deleteCmd);
        }
        LOG4CPLUS_INFO_EXT(mLogger, "Current app's menus removed!");

        switch(currentApp->getProtocolVersion())
        {
            case 1:
            {
                Application_v1* appV1 = (Application_v1*)currentApp;
                const ChoiceSetItems& currentChoiceSets = appV1->getAllChoiceSets();
                LOG4CPLUS_INFO_EXT(mLogger, "Removing current application's interaction choice sets from HMI");
                for(ChoiceSetItems::const_iterator it = currentChoiceSets.begin(); it != currentChoiceSets.end(); it++)
                {
                    const unsigned int& choiceSetId = it->first;
                    NsRPC2Communication::UI::DeleteInteractionChoiceSet* deleteCmd = new NsRPC2Communication::UI::DeleteInteractionChoiceSet();
                    deleteCmd->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    deleteCmd->set_interactionChoiceSetID(choiceSetId);
                    deleteCmd->set_appId(currentApp->getAppID());
                    mMessageMapping.addMessage(deleteCmd->getId(), appId);
                    HMIHandler::getInstance().sendRequest(deleteCmd);
                }
                LOG4CPLUS_INFO_EXT(mLogger, "Current app's interaction choice sets removed!");
                break;
            }
            case 2:
            {
                Application_v2* appV2 = (Application_v2*)currentApp;
                const ChoiceSetItems& currentChoiceSets = appV2->getAllChoiceSets();
                LOG4CPLUS_INFO_EXT(mLogger, "Removing current application's interaction choice sets from HMI");
                for(ChoiceSetItems::const_iterator it = currentChoiceSets.begin(); it != currentChoiceSets.end(); it++)
                {
                    const unsigned int& choiceSetId = it->first;
                    NsRPC2Communication::UI::DeleteInteractionChoiceSet* deleteCmd = new NsRPC2Communication::UI::DeleteInteractionChoiceSet();
                    deleteCmd->setId(HMIHandler::getInstance().getJsonRPC2Handler()->getNextMessageId());
                    deleteCmd->set_interactionChoiceSetID(choiceSetId);
                    deleteCmd->set_appId(currentApp->getAppID());
                    mMessageMapping.addMessage(deleteCmd->getId(), appId);
                    HMIHandler::getInstance().sendRequest(deleteCmd);
                }
                LOG4CPLUS_INFO_EXT(mLogger, "Current app's interaction choice sets removed!");
                break;
            }
        }
    }*/

    /**
     * \brief retrieve an application instance from the RegistryItrem instance checking for non-null values
     * \param item a RegistryItem from which to retrieve an app pointer
     * \return Application instance retrieved from item
     */
    Application *AppMgrCore::getApplicationFromItemCheckNotNull(const RegistryItem *item) const
    {
        if(!item)
        {
            LOG4CPLUS_WARN_EXT(mLogger, "No registry item found!");
            return 0;
        }
        Application* app = item->getApplication();
        if(!app)
        {
            LOG4CPLUS_WARN_EXT(mLogger, "No application associated with this registry item!");
            return 0;
        }
        return app;
    }

    /**
     * \brief set Json mobile handler
     * \param handler a handler instance
     */
    void AppMgrCore::setJsonHandler(JSONHandler* handler)
    {
        if(!handler)
        {
            LOG4CPLUS_ERROR_EXT(mLogger, "A null pointer is being assigned - is this the intent?");
            return;
        }
        MobileHandler::getInstance().setJsonHandler(handler);
    }

    /**
     * \brief get Json mobile handler
     * \return JSONHandler instance
     */
    JSONHandler* AppMgrCore::getJsonHandler( ) const
    {
        return MobileHandler::getInstance().getJsonHandler();
    }

    /**
     * \brief set Json RPC2 handler
     * \param handler a handler instance
     */
    void AppMgrCore::setJsonRPC2Handler(JSONRPC2Handler *handler)
    {
        if(!handler)
        {
            LOG4CPLUS_ERROR_EXT(mLogger, "A null pointer is being assigned - is this the intent?");
            return;
        }
        HMIHandler::getInstance().setJsonRPC2Handler(handler);
    }

    /**
     * \brief get Json RPC2 handler
     * \return JSONRPC2Handler instance
     */
    JSONRPC2Handler *AppMgrCore::getJsonRPC2Handler() const
    {
        return HMIHandler::getInstance().getJsonRPC2Handler();
    }

    /**
     * \brief Sets connection handler instance
     * \param handler connection handler
     */
    void AppMgrCore::setConnectionHandler(NsConnectionHandler::IDevicesDiscoveryStarter *handler)
    {
        if(!handler)
        {
            LOG4CPLUS_ERROR_EXT(mLogger, "A null pointer is being assigned - is this the intent?");
            return;
        }
        ConnectionHandler::getInstance().setConnectionHandler(handler);
    }

    /**
     * \brief Gets connection handler instance
     * \return connection handler
     */
    NsConnectionHandler::IDevicesDiscoveryStarter *AppMgrCore::getConnectionHandler() const
    {
        return ConnectionHandler::getInstance().getConnectionHandler();
    }

    /**
     * \brief set device list
     * \param deviceList device list
     */
    void AppMgrCore::setDeviceList(const NsConnectionHandler::tDeviceList &deviceList)
    {
        LOG4CPLUS_INFO_EXT(mLogger, " Updating device list: " << deviceList.size() << " devices");
        //mDeviceList.setDeviceList(deviceList);
        differenceBetweenLists(deviceList);
        NsRPC2Communication::BasicCommunication::OnDeviceListUpdated* deviceListUpdated =
                new NsRPC2Communication::BasicCommunication::OnDeviceListUpdated;
        if ( !deviceList.empty() )
        {
            std::vector<std::string> deviceNames;
            for(std::map<int, DeviceStorage>::const_iterator it = mDevices.begin();
                it != mDevices.end(); ++it)
            {
                deviceNames.push_back(it->second.getUserFriendlyName());
            }
            deviceListUpdated->set_deviceList(deviceNames);
        }
        HMIHandler::getInstance().sendNotification(deviceListUpdated);
    }

    void AppMgrCore::differenceBetweenLists( const NsConnectionHandler::tDeviceList &deviceList )
    {
        LOG4CPLUS_INFO_EXT(mLogger, "Start finding diff.");
        typedef NsConnectionHandler::tDeviceList::const_iterator NewIterator;
        typedef std::map<int, DeviceStorage>::const_iterator ExistingIterator;
        typedef std::map<int, Application*>::const_iterator ApplicationIterator;

        for(NewIterator it = deviceList.begin(); it != deviceList.end(); ++it)
        {
            LOG4CPLUS_INFO_EXT(mLogger, "\t\t\t\tNew device " << it->first);
        }
        for(ExistingIterator it = mDevices.begin(); it != mDevices.end(); ++it)
        {
            LOG4CPLUS_INFO_EXT(mLogger, "\t\t\t\tOld device " << it->first);
        }

        std::map<int, DeviceStorage> updatedMap;

        ExistingIterator tempIt = mDevices.end();
        ExistingIterator oldIt = mDevices.begin();
        NewIterator newIt = deviceList.begin();

        while ( newIt != deviceList.end() )
        {
            LOG4CPLUS_INFO_EXT(mLogger, "Current device " << newIt->first);
            if ( mDevices.end() == oldIt )
            {
                LOG4CPLUS_INFO_EXT(mLogger, "End of existing devices.");
                for( NewIterator newItRest = newIt; newItRest != deviceList.end(); ++ newItRest)
                {
                    updatedMap.insert( std::pair<int, DeviceStorage>(newItRest->first,
                        DeviceStorage(newItRest->second.getDeviceHandle(), newItRest->second.getUserFriendlyName())) );
                }
                LOG4CPLUS_INFO_EXT(mLogger, "Size of updated map " << updatedMap.size());
                break;
            }

            while( oldIt != mDevices.end() && newIt != deviceList.end() && newIt->first > oldIt->first )
            {
                LOG4CPLUS_INFO_EXT(mLogger, "\t\t\t\tRemoving old device " << oldIt->first);
                if( oldIt->second.getAppId() )
                {
                    // The app should be deleted.
                    ApplicationIterator appIt = mApplications.find( oldIt->second.getAppId() );
                    if ( mApplications.end() != appIt )
                    {
                        if ( appIt->second )
                        {
                            LOG4CPLUS_INFO_EXT(mLogger, "Unregistering app " << appIt->first);
                            unregisterApplication( appIt -> first );
                            //Application * appToBeRemoved =
                            //unregister app
                        }
                        //mApplications.erase( appIt );
                    }
                }
                ++oldIt;
            }

            while( oldIt != mDevices.end() && newIt != deviceList.end() && newIt->first < oldIt->first )
            {
                LOG4CPLUS_INFO_EXT(mLogger, "\t\t\t\tAdding new device " << newIt->first);
                updatedMap.insert( std::pair<int, DeviceStorage>(newIt->first,
                            DeviceStorage(newIt->second.getDeviceHandle(), newIt->second.getUserFriendlyName())) );
                ++newIt;
            }
            if ( oldIt != mDevices.end() && newIt != deviceList.end() && newIt->first == oldIt->first )
            {
                LOG4CPLUS_INFO_EXT(mLogger, "\t\t\t\tCopying existing device " << oldIt->first);
                updatedMap.insert( std::pair<int, DeviceStorage>(newIt->first,
                            DeviceStorage(newIt->second.getDeviceHandle(), newIt->second.getUserFriendlyName())) );
                ++oldIt; ++newIt;
            }
        }

        if ( mDevices.end() != oldIt )
        {
            //oldIt = (mDevices.begin() == oldIt ? oldIt : --oldIt);
            for( ExistingIterator it = oldIt; it != mDevices.end(); ++it )
            {
                LOG4CPLUS_INFO_EXT(mLogger, "Removing old devices");
                if( it->second.getAppId() )
                {
                    // The app should be deleted.
                    ApplicationIterator appIt = mApplications.find( it->second.getAppId() );
                    if ( mApplications.end() != appIt )
                    {
                        if ( appIt->second )
                        {
                            LOG4CPLUS_INFO_EXT(mLogger, "Unregistering app " << appIt->first);
                            unregisterApplication( appIt -> first );
                        }
                    }
                }
            }
        }

        LOG4CPLUS_INFO_EXT(mLogger, "size of existing devices " << mDevices.size());
        mDevices = updatedMap;
        LOG4CPLUS_INFO_EXT(mLogger, "size of existing devices " << mDevices.size());
    }

    /**
     * \brief get device list
     * \return device list
     */
    /*const NsConnectionHandler::tDeviceList &AppMgrCore::getDeviceList() const
    {
        return mDeviceList.getDeviceList();
    }*/

    /**
     * \brief add a device to a mapping
     * \param connectionKey session/connection key
     * \param device device handler
     */
    void AppMgrCore::addDevice(const NsConnectionHandler::tDeviceHandle &device,
            const int &sessionKey, int firstSessionKey)
    {
        //mDeviceHandler.addDevice(sessionKey, device);
        LOG4CPLUS_INFO_EXT(mLogger, "\n\t\t\t\tAdding session to device " <<
                device << " with first session " << firstSessionKey
                << " session: " << sessionKey);
        if( sessionKey == firstSessionKey )
        {
            LOG4CPLUS_INFO_EXT(mLogger, "\n\t\t\t\tAdding first session " << firstSessionKey);
            std::map<int, DeviceStorage>::iterator it = mDevices.find(device);
            if ( mDevices.end() != it )
            {
                it->second.setAppId(sessionKey);
                LOG4CPLUS_INFO_EXT(mLogger, "\n\t\t\t\tAdded session to device " << it->first);
            }
        }
        else
        {
            for(std::map<int, DeviceStorage>::iterator it = mDevices.begin();
                    it != mDevices.end(); ++it)
            {
                if ( it->second.getAppId() == firstSessionKey )
                {
                    it->second.addConnectionKey( sessionKey );
                    LOG4CPLUS_INFO_EXT(mLogger, "\n\t\t\t\tSession "<<sessionKey
                         << " added to device " << it->first);
                    break;
                }
            }
        }
    }

    /**
     * \brief remove a device from a mapping
     * \param sessionKey session/connection key
     */
    void AppMgrCore::removeDevice(const int &sessionKey, int firstSessionKey)
    {
        LOG4CPLUS_INFO_EXT(mLogger, "\n\t\t\t\tRemoving session " << sessionKey
                << " with first session " << firstSessionKey);
        if ( sessionKey == firstSessionKey )
        {
            std::map<int, Application *>::iterator it = mApplications.find( firstSessionKey );
            if ( it != mApplications.end() )
            {
                LOG4CPLUS_INFO_EXT(mLogger, "\n\t\t\t\tRemoving app from device " << it->second->getDeviceHandle());
                mDevices.erase( it->second->getDeviceHandle() );
            }
            unregisterApplication( firstSessionKey );
        }
        else
        {
            for(std::map<int, DeviceStorage>::iterator it = mDevices.begin();
                    it != mDevices.end(); ++it)
            {
                if ( it->second.getAppId() == firstSessionKey )
                {
                    it->second.removeConnectionKey( sessionKey );
                    LOG4CPLUS_INFO_EXT(mLogger, "\n\t\t\t\tsession " << sessionKey
                        << " revmoved from device " << it->first);
                    // TODO(PV): remove from applist if all sessions are closed.
                    mApplications.erase( sessionKey );
                    break;
                }
            }
        }
    }

    bool AppMgrCore::getAudioPassThruFlag() const
    {
        return mAudioPassThruFlag;
    }

    void AppMgrCore::setAudioPassThruFlag(bool flag)
    {
        mAudioPassThruFlag = flag;
    }

    /*const MessageMapping& AppMgrCore::getMessageMapping() const
    {
        return mMessageMapping;
    }*/

    void AppMgrCore::sendButtonEvent( Application * app, NsRPC2Communication::Buttons::OnButtonEvent * object )
    {
        int appId = app->getAppID();

        switch(app->getProtocolVersion())
        {
            case 1:
            {
                NsSmartDeviceLinkRPC::OnButtonEvent* event = new NsSmartDeviceLinkRPC::OnButtonEvent();
                event->set_buttonEventMode(static_cast<NsSmartDeviceLinkRPC::ButtonEventMode::ButtonEventModeInternal>(object->get_mode().get()));
                event->set_buttonName(
                    static_cast<NsSmartDeviceLinkRPC::ButtonName::ButtonNameInternal>(
                        object->get_name().get()));
                LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName() << " Application id " << appId);
                MobileHandler::getInstance().sendRPCMessage(event, appId);
                break;
            }
            case 2:
            {
                NsSmartDeviceLinkRPCV2::OnButtonEvent* event = new NsSmartDeviceLinkRPCV2::OnButtonEvent();
                event->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnButtonEventID);
                event->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                event->set_buttonEventMode(object->get_mode());
                event->set_buttonName(object->get_name());
                if(object->get_customButtonID())
                {
                    event->set_customButtonID(*object->get_customButtonID());
                }
                else
                {
                    event->set_customButtonID(0);
                }
                event->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                event->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnButtonEventID);
                LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName() << " application id " << appId);
                MobileHandler::getInstance().sendRPCMessage(event, appId);
                break;
            }
        }
    }

    void AppMgrCore::sendButtonPress( Application * app, NsRPC2Communication::Buttons::OnButtonPress * object )
    {
        int appId = app->getAppID();

        switch(app->getProtocolVersion())
        {
            case 1:
            {
                NsSmartDeviceLinkRPC::OnButtonPress* event = new NsSmartDeviceLinkRPC::OnButtonPress();

                event->set_buttonName(static_cast<NsSmartDeviceLinkRPC::ButtonName::ButtonNameInternal>(
                        object->get_name().get()));
                event->set_buttonPressMode(static_cast<NsSmartDeviceLinkRPC::ButtonPressMode::ButtonPressModeInternal>(object->get_mode().get()));
                LOG4CPLUS_INFO_EXT(mLogger, "before we find sessionID");

                LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app " << app->getName()
                    << " application id " << appId);
                MobileHandler::getInstance().sendRPCMessage(event, appId);
                break;
            }
            case 2:
            {
                NsSmartDeviceLinkRPCV2::OnButtonPress* event = new NsSmartDeviceLinkRPCV2::OnButtonPress();
                event->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnButtonPressID);
                event->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                event->set_buttonName(object->get_name());
                NsSmartDeviceLinkRPCV2::ButtonPressMode pressMode;
                pressMode.set((NsSmartDeviceLinkRPCV2::ButtonPressMode::ButtonPressModeInternal)object->get_mode().get());
                event->set_buttonPressMode(pressMode);
                if(object->get_customButtonID())
                {
                    event->set_customButtonID(*object->get_customButtonID());
                }
                else
                {
                    event->set_customButtonID(0);
                }
                LOG4CPLUS_INFO_EXT(mLogger, "before we find sessionID");

                event->setMessageType(NsSmartDeviceLinkRPC::SDLRPCMessage::NOTIFICATION);
                event->setMethodId(NsSmartDeviceLinkRPCV2::FunctionID::OnButtonPressID);

                LOG4CPLUS_INFO_EXT(mLogger, " A message will be sent to an app "<< app->getName()
                    << " application id " << appId);
                MobileHandler::getInstance().sendRPCMessage(event, appId);
                break;
            }
        }
    }

    bool AppMgrCore::decreaseMessageChain(const MessageChains::iterator & chain)
    {
        chain->second->counter--;
        MessageChaining * currentChain = chain->second;
        mMessageChaining.erase(chain);
        if (!currentChain->counter)
        {
            delete currentChain;
            return true;
        }
        return false;
    }

    MessageChaining * AppMgrCore::addChain(MessageChaining * chain, int sessionKey, unsigned int correlationID)
    {
        if (!chain)
        {
            chain = new MessageChaining(
                sessionKey,
                correlationID);
        }
        else
        {
            chain->counter++;
        }
        return chain;
    }

    Application * AppMgrCore::getActiveItem()
    {
        for(std::map<int,Application*>::const_iterator it = mApplications.begin();
                it != mApplications.end();
                ++it)
        {
            if( NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL == it->second->getApplicationHMIStatusLevel() )
            {
                return it->second;
            }
        }
        return 0;
    }

    Application * AppMgrCore::getItem( int applicationId )
    {
        std::map<int, Application*>::iterator it = mApplications.find( applicationId );
        if( mApplications.end() != it )
        {
            return it->second;
        }
        return 0;
    }

    bool AppMgrCore::activateApp( Application * appToBeActivated )
    {
        if(!appToBeActivated)
        {
            LOG4CPLUS_ERROR_EXT(mLogger, " Cannot activate null-application!");
            return false;
        }
        LOG4CPLUS_INFO_EXT(mLogger, "Activating an app " << appToBeActivated->getName() << " application id " << appToBeActivated->getAppID());

        bool isCurrentAudible = false;
        for(std::map<int,Application*>::iterator it = mApplications.begin();
                it != mApplications.end();
                ++it)
        {
            Application* tempApplication = it->second;
            if(!tempApplication)
            {
                LOG4CPLUS_WARN_EXT(mLogger, "No application for the item!");
                return false;
            }

            if ( tempApplication->getAppID() == appToBeActivated->getAppID() )
            {
                continue;
            }

            // TODO(PV): at this point it is assumed that behaviour is correct and
            // when app is not in HMI_FULL/HMI_LIMITED it is not audible.
            if ( tempApplication->getIsMediaApplication()
                    && NsSmartDeviceLinkRPCV2::AudioStreamingState::AUDIBLE
                            == tempApplication->getApplicationAudioStreamingState().get()
                    && appToBeActivated->getIsMediaApplication() )
            {
                tempApplication->setApplicationAudioStreamingState(
                    NsSmartDeviceLinkRPCV2::AudioStreamingState::NOT_AUDIBLE);
            }

            if ( NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL == tempApplication->getApplicationHMIStatusLevel()
                || ( NsSmartDeviceLinkRPCV2::HMILevel::HMI_LIMITED == tempApplication->getApplicationHMIStatusLevel()
                    && appToBeActivated->getIsMediaApplication() ) )
            {
                LOG4CPLUS_INFO_EXT(mLogger, " Deactivating application " << tempApplication->getName());
                tempApplication -> setApplicationHMIStatusLevel(
                    NsSmartDeviceLinkRPCV2::HMILevel::HMI_BACKGROUND);
                AppMgrCore::getInstance().sendHMINotificationToMobile(tempApplication);
            }
        }

        LOG4CPLUS_INFO_EXT(mLogger, " Activating application " << appToBeActivated->getName());
        appToBeActivated->setApplicationHMIStatusLevel(NsSmartDeviceLinkRPCV2::HMILevel::HMI_FULL);
        appToBeActivated->setSystemContext(NsSmartDeviceLinkRPCV2::SystemContext::SYSCTXT_MAIN);
        if ( appToBeActivated->getIsMediaApplication() )
        {
            appToBeActivated->setApplicationAudioStreamingState(NsSmartDeviceLinkRPCV2::AudioStreamingState::AUDIBLE);
        }
        else
        {
            appToBeActivated->setApplicationAudioStreamingState(NsSmartDeviceLinkRPCV2::AudioStreamingState::NOT_AUDIBLE);
        }
        AppMgrCore::getInstance().sendHMINotificationToMobile(appToBeActivated);

        return true;
    }

    Application * AppMgrCore::getApplicationByCommand(const unsigned int &cmdId, int appId)
    {
        std::map<int, Application*>::iterator it = mApplications.find(appId);
        if ( mApplications.end() != it )
        {
            Application* app = it->second;
            if(!app)
            {
                LOG4CPLUS_ERROR_EXT(mLogger, " No application for the item " << it->first);
                return 0;
            }
            if(app->getCommandsCount())
            {
                const Commands& cmds = app->findCommands(cmdId);
                if(!cmds.empty())
                {
                    return app;
                }
            }
        }
        /*for(std::map<int, Application*>::iterator it = mApplications.begin();
                it != mApplications.end();
                ++it)
        {
            Application* app = it->second;
            if(!app)
            {
                LOG4CPLUS_ERROR_EXT(mLogger, " No application for the item " << it->first);
                continue;
            }
            if(app->getCommandsCount())
            {
                const Commands& cmds = app->findCommands(cmdId);
                if(!cmds.empty())
                {
                    return app;
                }
            }
        }*/
        LOG4CPLUS_INFO_EXT(mLogger, " No applications found for the command " << cmdId);
        return 0;
    }
}
