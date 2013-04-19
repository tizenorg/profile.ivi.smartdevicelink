//
// Copyright (c) 2013 Ford Motor Company
//
package com.smartdevicelink.proxy;

import java.util.Hashtable;
import java.util.Vector;

import android.os.Handler;
import android.os.Looper;
import android.telephony.TelephonyManager;

import com.smartdevicelink.exception.SmartDeviceLinkException;
import com.smartdevicelink.exception.SmartDeviceLinkExceptionCause;
import com.smartdevicelink.marshal.JsonRPCMarshaller;
import com.smartdevicelink.messageDispatcher.IDispatchingStrategy;
import com.smartdevicelink.messageDispatcher.IncomingProtocolMessageComparitor;
import com.smartdevicelink.messageDispatcher.InternalProxyMessageComparitor;
import com.smartdevicelink.messageDispatcher.OutgoingProtocolMessageComparitor;
import com.smartdevicelink.messageDispatcher.ProxyMessageDispatcher;
import com.smartdevicelink.protocol.ProtocolMessage;
import com.smartdevicelink.protocol.enums.FunctionID;
import com.smartdevicelink.protocol.enums.MessageType;
import com.smartdevicelink.protocol.enums.SessionType;
import com.smartdevicelink.proxy.callbacks.InternalProxyMessage;
import com.smartdevicelink.proxy.callbacks.OnProxyClosed;
import com.smartdevicelink.proxy.callbacks.OnError;
import com.smartdevicelink.proxy.constants.Names;
import com.smartdevicelink.proxy.interfaces.IProxyListenerBase;
import com.smartdevicelink.proxy.rpc.AddCommand;
import com.smartdevicelink.proxy.rpc.AddCommandResponse;
import com.smartdevicelink.proxy.rpc.AddSubMenu;
import com.smartdevicelink.proxy.rpc.AddSubMenuResponse;
import com.smartdevicelink.proxy.rpc.Alert;
import com.smartdevicelink.proxy.rpc.AlertResponse;
import com.smartdevicelink.proxy.rpc.ButtonCapabilities;
import com.smartdevicelink.proxy.rpc.Choice;
import com.smartdevicelink.proxy.rpc.CreateInteractionChoiceSet;
import com.smartdevicelink.proxy.rpc.CreateInteractionChoiceSetResponse;
import com.smartdevicelink.proxy.rpc.DeleteCommand;
import com.smartdevicelink.proxy.rpc.DeleteCommandResponse;
import com.smartdevicelink.proxy.rpc.DeleteInteractionChoiceSet;
import com.smartdevicelink.proxy.rpc.DeleteInteractionChoiceSetResponse;
import com.smartdevicelink.proxy.rpc.DeleteSubMenu;
import com.smartdevicelink.proxy.rpc.DeleteSubMenuResponse;
import com.smartdevicelink.proxy.rpc.DisplayCapabilities;
import com.smartdevicelink.proxy.rpc.EncodedSyncPData;
import com.smartdevicelink.proxy.rpc.EncodedSyncPDataResponse;
import com.smartdevicelink.proxy.rpc.GenericResponse;
import com.smartdevicelink.proxy.rpc.OnAppInterfaceUnregistered;
import com.smartdevicelink.proxy.rpc.OnButtonEvent;
import com.smartdevicelink.proxy.rpc.OnButtonPress;
import com.smartdevicelink.proxy.rpc.OnCommand;
import com.smartdevicelink.proxy.rpc.OnDriverDistraction;
import com.smartdevicelink.proxy.rpc.OnEncodedSyncPData;
import com.smartdevicelink.proxy.rpc.OnHMIStatus;
import com.smartdevicelink.proxy.rpc.OnPermissionsChange;
import com.smartdevicelink.proxy.rpc.OnTBTClientState;
import com.smartdevicelink.proxy.rpc.PerformInteraction;
import com.smartdevicelink.proxy.rpc.PerformInteractionResponse;
import com.smartdevicelink.proxy.rpc.RegisterAppInterface;
import com.smartdevicelink.proxy.rpc.RegisterAppInterfaceResponse;
import com.smartdevicelink.proxy.rpc.ResetGlobalProperties;
import com.smartdevicelink.proxy.rpc.ResetGlobalPropertiesResponse;
import com.smartdevicelink.proxy.rpc.SetGlobalProperties;
import com.smartdevicelink.proxy.rpc.SetGlobalPropertiesResponse;
import com.smartdevicelink.proxy.rpc.SetMediaClockTimer;
import com.smartdevicelink.proxy.rpc.SetMediaClockTimerResponse;
import com.smartdevicelink.proxy.rpc.Show;
import com.smartdevicelink.proxy.rpc.ShowResponse;
import com.smartdevicelink.proxy.rpc.Speak;
import com.smartdevicelink.proxy.rpc.SpeakResponse;
import com.smartdevicelink.proxy.rpc.SubscribeButton;
import com.smartdevicelink.proxy.rpc.SubscribeButtonResponse;
import com.smartdevicelink.proxy.rpc.SyncMsgVersion;
import com.smartdevicelink.proxy.rpc.TTSChunk;
import com.smartdevicelink.proxy.rpc.UnregisterAppInterface;
import com.smartdevicelink.proxy.rpc.UnregisterAppInterfaceResponse;
import com.smartdevicelink.proxy.rpc.UnsubscribeButton;
import com.smartdevicelink.proxy.rpc.UnsubscribeButtonResponse;
import com.smartdevicelink.proxy.rpc.enums.AudioStreamingState;
import com.smartdevicelink.proxy.rpc.enums.ButtonName;
import com.smartdevicelink.proxy.rpc.enums.GlobalProperty;
import com.smartdevicelink.proxy.rpc.enums.HMILevel;
import com.smartdevicelink.proxy.rpc.enums.HmiZoneCapabilities;
import com.smartdevicelink.proxy.rpc.enums.InteractionMode;
import com.smartdevicelink.proxy.rpc.enums.Language;
import com.smartdevicelink.proxy.rpc.enums.SpeechCapabilities;
import com.smartdevicelink.proxy.rpc.enums.SmartDeviceLinkConnectionState;
import com.smartdevicelink.proxy.rpc.enums.SmartDeviceLinkDisconnectedReason;
import com.smartdevicelink.proxy.rpc.enums.SmartDeviceLinkInterfaceAvailability;
import com.smartdevicelink.proxy.rpc.enums.SystemContext;
import com.smartdevicelink.proxy.rpc.enums.TextAlignment;
import com.smartdevicelink.proxy.rpc.enums.UpdateMode;
import com.smartdevicelink.proxy.rpc.enums.VrCapabilities;
import com.smartdevicelink.syncConnection.ISmartDeviceLinkConnectionListener;
import com.smartdevicelink.syncConnection.SmartDeviceLinkConnection;
import com.smartdevicelink.trace.TraceDeviceInfo;
import com.smartdevicelink.trace.SyncTrace;
import com.smartdevicelink.trace.enums.InterfaceActivityDirection;
import com.smartdevicelink.transport.BTTransportConfig;
import com.smartdevicelink.transport.BaseTransportConfig;
import com.smartdevicelink.transport.SiphonServer;
import com.smartdevicelink.transport.TransportType;
import com.smartdevicelink.util.DebugTool;

public abstract class SmartDeviceLinkProxyBase<proxyListenerType extends IProxyListenerBase> {
	// Used for calls to Android Log class.
	public static final String TAG = "SmartDeviceLinkProxy";
	private static final String SMARTDEVICELINK_LIB_TRACE_KEY = "42baba60-eb57-11df-98cf-0800200c9a66";
	private SmartDeviceLinkConnection _SmartDeviceLinkConnection;
	private proxyListenerType _proxyListener = null;
	
	// Protected Correlation IDs
	private final int 	REGISTER_APP_INTERFACE_CORRELATION_ID = 65529,
						UNREGISTER_APP_INTERFACE_CORRELATION_ID = 65530;
	
	// SmartDeviceLinkhronization Objects
	private static final Object CONNECTION_REFERENCE_LOCK = new Object(),
								INCOMING_MESSAGE_QUEUE_THREAD_LOCK = new Object(),
								OUTGOING_MESSAGE_QUEUE_THREAD_LOCK = new Object(),
								INTERNAL_MESSAGE_QUEUE_THREAD_LOCK = new Object(),
								APP_INTERFACE_REGISTERED_LOCK = new Object();
	
	// RPC Session ID
	private byte _rpcSessionID = 0;
	
	// Device Info for logging
	private TraceDeviceInfo _traceDeviceInterrogator = null;
		
	// Declare Queuing Threads
	private ProxyMessageDispatcher<ProtocolMessage> _incomingProxyMessageDispatcher;
	private ProxyMessageDispatcher<ProtocolMessage> _outgoingProxyMessageDispatcher;
	private ProxyMessageDispatcher<InternalProxyMessage> _internalProxyMessageDispatcher;
	
	// Flag indicating if callbacks should be called from UIThread
	private Boolean _callbackToUIThread = false;
	// UI Handler
	private Handler _mainUIHandler = null; 
	
	// SmartDeviceLinkProxy Advanced Lifecycle Management
	protected Boolean _advancedLifecycleManagementEnabled = false;
	// Parameters passed to the constructor from the app to register an app interface
	private String _applicationName = null;
	private String _ngnMediaScreenAppName = null;
	private Boolean _isMediaApp = null;
	private Language _SmartDeviceLinkLanguageDesired = null;
	private String _autoActivateIdDesired = null;
	private SyncMsgVersion _SyncMsgVersionRequest = null;
	private Vector<String> _vrSynonyms = null;

	private BaseTransportConfig _transportConfig = null;
	// Proxy State Variables
	protected Boolean _appInterfaceRegisterd = false;
	protected Boolean _haveReceivedFirstFocusLevel = false;
	protected Boolean _haveReceivedFirstFocusLevelFull = false;
	protected Boolean _proxyDisposed = false;
	protected SmartDeviceLinkConnectionState _SmartDeviceLinkConnectionState = null;
	protected SmartDeviceLinkInterfaceAvailability _SmartDeviceLinkIntefaceAvailablity = null;
	protected HMILevel _hmiLevel = null;
	private HMILevel _priorHmiLevel = null;
	protected AudioStreamingState _audioStreamingState = null;
	private AudioStreamingState _priorAudioStreamingState = null;
	protected SystemContext _systemContext = null;
	// Variables set by RegisterAppInterfaceResponse
	protected SyncMsgVersion _SyncMsgVersion = null;
	protected String _autoActivateIdReturned = null;
	protected Language _SmartDeviceLinkLanguage = null;
	protected DisplayCapabilities _displayCapabilities = null;
	protected Vector<ButtonCapabilities> _buttonCapabilities = null;
	protected Vector<HmiZoneCapabilities> _hmiZoneCapabilities = null;
	protected Vector<SpeechCapabilities> _speechCapabilities = null;
	protected Vector<VrCapabilities> _vrCapabilities = null;
	protected Boolean firstTimeFull = true;
	
	protected byte _protocolVersion = 1;
	
	// Interface broker
	private SmartDeviceLinkInterfaceBroker _interfaceBroker = null;
	
	// Private Class to Interface with SmartDeviceLinkConnection
	private class SmartDeviceLinkInterfaceBroker implements ISmartDeviceLinkConnectionListener {
		
		@Override
		public void onTransportDisconnected(String info) {
			// proxyOnTransportDisconnect is called to alert the proxy that a requested
			// disconnect has completed
			
			if (_advancedLifecycleManagementEnabled) {
				// If ALM, nothing is required to be done here
			} else {
				// If original model, notify app the proxy is closed so it will delete and reinstanciate 
				notifyProxyClosed(info, new SmartDeviceLinkException("Transport disconnected.", SmartDeviceLinkExceptionCause.SMARTDEVICELINK_UNAVAILALBE));
			}
		}

		@Override
		public void onTransportError(String info, Exception e) {
			DebugTool.logError("Transport failure: " + info, e);
			
			if (_advancedLifecycleManagementEnabled) {			
				// Cycle the proxy
				cycleProxy(SmartDeviceLinkDisconnectedReason.TRANSPORT_ERROR);
			} else {
				notifyProxyClosed(info, e);
			}
		}

		@Override
		public void onProtocolMessageReceived(ProtocolMessage msg) {
			try {if (msg.getData().length > 0) queueIncomingMessage(msg);}
			catch (Exception e) {}
			try {if (msg.getBulkData().length > 0) queueIncomingMessage(msg);}
			catch (Exception e) {}
		}

		@Override
		public void onProtocolSessionStarted(SessionType sessionType,
				byte sessionID, byte version, String correlationID) {
			if (_protocolVersion == 1) {
				if (version == 2) setProtocolVersion(version);
			}
			if (sessionType.eq(SessionType.RPC)) {			
				startRPCProtocolSession(sessionID, correlationID);
			} else if (_protocolVersion == 2) {
				//If version 2 then don't need to specify a Session Type
				startRPCProtocolSession(sessionID, correlationID);
			}  else {
				// Handle other protocol session types here
			}
		}

		@Override
		public void onProtocolSessionEnded(SessionType sessionType,
				byte sessionID, String correlationID) {
			// How to handle protocol session ended?
				// How should protocol session management occur? 
		}

		@Override
		public void onProtocolError(String info, Exception e) {
			passErrorToProxyListener(info, e);
		}
	}

	protected SmartDeviceLinkProxyBase(proxyListenerType listener, SmartDeviceLinkProxyConfigurationResources SmartDeviceLinkProxyConfigurationResources, 
			boolean enableAdvancedLifecycleManagement, String appName, String ngnMediaScreenAppName, 
			Vector<String> vrSynonyms, Boolean isMediaApp, SyncMsgVersion SyncMsgVersion, 
			Language languageDesired, String autoActivateID, boolean callbackToUIThread,
			BaseTransportConfig transportConfig) 
			throws SmartDeviceLinkException {
		
		_interfaceBroker = new SmartDeviceLinkInterfaceBroker();
		
		_callbackToUIThread = callbackToUIThread;
		
		if (_callbackToUIThread) {
			_mainUIHandler = new Handler(Looper.getMainLooper());
		}
		
		// Set variables for Advanced Lifecycle Management
		_advancedLifecycleManagementEnabled = enableAdvancedLifecycleManagement;
		_applicationName = appName;
		_ngnMediaScreenAppName = ngnMediaScreenAppName;
		_isMediaApp = isMediaApp;
		_SyncMsgVersionRequest = SyncMsgVersion;
		_vrSynonyms = vrSynonyms; 
		_SmartDeviceLinkLanguageDesired = languageDesired;
		_autoActivateIdDesired = autoActivateID;
		_transportConfig = transportConfig;
		
		// Test conditions to invalidate the proxy
		if (listener == null) {
			throw new IllegalArgumentException("IProxyListener listener must be provided to instantiate SmartDeviceLinkProxy object.");
		}
		if (_advancedLifecycleManagementEnabled) {
			if (_applicationName == null ) {
				throw new IllegalArgumentException("To use SmartDeviceLinkProxyALM, an application name, appName, must be provided");
			}
			if (_applicationName.length() < 1 || _applicationName.length() > 100) {
				throw new IllegalArgumentException("A provided application name, appName, must be between 1 and 100 characters in length.");
			}
			if (_isMediaApp == null) {
				throw new IllegalArgumentException("isMediaApp must not be null when using SmartDeviceLinkProxyALM.");
			}
		}
		
		_proxyListener = listener;
		
		// Get information from SmartDeviceLinkProxyConfigurationResources
		TelephonyManager telephonyManager = null;
		if (SmartDeviceLinkProxyConfigurationResources != null) {
			telephonyManager = SmartDeviceLinkProxyConfigurationResources.getTelephonyManager();
		} 
		
		// Use the telephonyManager to get and log phone info
		if (telephonyManager != null) {
			// Following is not quite thread-safe (because m_traceLogger could test null twice),
			// so we need to fix this, but vulnerability (i.e. two instances of listener) is
			// likely harmless.
			if (_traceDeviceInterrogator == null) {
				_traceDeviceInterrogator = new TraceDeviceInfo(SmartDeviceLinkProxyConfigurationResources.getTelephonyManager());
			} // end-if
		} // end-if
		
		// Setup Internal ProxyMessage Dispatcher
		synchronized(INTERNAL_MESSAGE_QUEUE_THREAD_LOCK) {
			// Ensure internalProxyMessageDispatcher is null
			if (_internalProxyMessageDispatcher != null) {
				_internalProxyMessageDispatcher.dispose();
				_internalProxyMessageDispatcher = null;
			}
			
			_internalProxyMessageDispatcher = new ProxyMessageDispatcher<InternalProxyMessage>("INTERNAL_MESSAGE_DISPATCHER",
					new InternalProxyMessageComparitor(),
					new IDispatchingStrategy<InternalProxyMessage>() {

						@Override
						public void dispatch(InternalProxyMessage message) {
							dispatchInternalMessage((InternalProxyMessage)message);
						}
	
						@Override
						public void handleDispatchingError(String info, Exception ex) {
							handleErrorsFromInternalMessageDispatcher(info, ex);
						}
	
						@Override
						public void handleQueueingError(String info, Exception ex) {
							handleErrorsFromInternalMessageDispatcher(info, ex);
						}			
			});
		}
		
		// Setup Incoming ProxyMessage Dispatcher
		synchronized(INCOMING_MESSAGE_QUEUE_THREAD_LOCK) {
			// Ensure incomingProxyMessageDispatcher is null
			if (_incomingProxyMessageDispatcher != null) {
				_incomingProxyMessageDispatcher.dispose();
				_incomingProxyMessageDispatcher = null;
			}
			
			_incomingProxyMessageDispatcher = new ProxyMessageDispatcher<ProtocolMessage>("INCOMING_MESSAGE_DISPATCHER",
					new IncomingProtocolMessageComparitor(),
					new IDispatchingStrategy<ProtocolMessage>() {
						@Override
						public void dispatch(ProtocolMessage message) {
							dispatchIncomingMessage((ProtocolMessage)message);
						}
	
						@Override
						public void handleDispatchingError(String info, Exception ex) {
							handleErrorsFromIncomingMessageDispatcher(info, ex);
						}
	
						@Override
						public void handleQueueingError(String info, Exception ex) {
							handleErrorsFromIncomingMessageDispatcher(info, ex);
						}			
			});
		}
		
		// Setup Outgoing ProxyMessage Dispatcher
		synchronized(OUTGOING_MESSAGE_QUEUE_THREAD_LOCK) {
			// Ensure outgoingProxyMessageDispatcher is null
			if (_outgoingProxyMessageDispatcher != null) {
				_outgoingProxyMessageDispatcher.dispose();
				_outgoingProxyMessageDispatcher = null;
			}
			
			_outgoingProxyMessageDispatcher = new ProxyMessageDispatcher<ProtocolMessage>("OUTGOING_MESSAGE_DISPATCHER",
					new OutgoingProtocolMessageComparitor(),
					new IDispatchingStrategy<ProtocolMessage>() {
						@Override
						public void dispatch(ProtocolMessage message) {
							dispatchOutgoingMessage((ProtocolMessage)message);
						}
	
						@Override
						public void handleDispatchingError(String info, Exception ex) {
							handleErrorsFromOutgoingMessageDispatcher(info, ex);
						}
	
						@Override
						public void handleQueueingError(String info, Exception ex) {
							handleErrorsFromOutgoingMessageDispatcher(info, ex);
						}
			});
		}
		
		// Initialize the proxy
		try {
			initializeProxy();
		} catch (SmartDeviceLinkException e) {
			// Couldn't initialize the proxy 
			// Dispose threads and then rethrow exception
			
			if (_internalProxyMessageDispatcher != null) {
				_internalProxyMessageDispatcher.dispose();
				_internalProxyMessageDispatcher = null;
			}
			if (_incomingProxyMessageDispatcher != null) {
				_incomingProxyMessageDispatcher.dispose();
				_incomingProxyMessageDispatcher = null;
			}
			if (_outgoingProxyMessageDispatcher != null) {
				_outgoingProxyMessageDispatcher.dispose();
				_outgoingProxyMessageDispatcher = null;
			}
			throw e;
		} 
		
		// Trace that ctor has fired
		SyncTrace.logProxyEvent("SmartDeviceLinkProxy Created, instanceID=" + this.toString(), SMARTDEVICELINK_LIB_TRACE_KEY);
	}
	
	protected SmartDeviceLinkProxyBase(proxyListenerType listener, SmartDeviceLinkProxyConfigurationResources SmartDeviceLinkProxyConfigurationResources, 
			boolean enableAdvancedLifecycleManagement, String appName, String ngnMediaScreenAppName, 
			Vector<String> vrSynonyms, Boolean isMediaApp, SyncMsgVersion SyncMsgVersion, 
			Language languageDesired, String autoActivateID, boolean callbackToUIThread) 
			throws SmartDeviceLinkException {
		
		_interfaceBroker = new SmartDeviceLinkInterfaceBroker();
		
		_callbackToUIThread = callbackToUIThread;
		
		if (_callbackToUIThread) {
			_mainUIHandler = new Handler(Looper.getMainLooper());
		}
		
		// Set variables for Advanced Lifecycle Management
		_advancedLifecycleManagementEnabled = enableAdvancedLifecycleManagement;
		_applicationName = appName;
		_ngnMediaScreenAppName = ngnMediaScreenAppName;
		_isMediaApp = isMediaApp;
		_SyncMsgVersionRequest = SyncMsgVersion;
		_vrSynonyms = vrSynonyms; 
		_SmartDeviceLinkLanguageDesired = languageDesired;
		_autoActivateIdDesired = autoActivateID;
		_transportConfig = new BTTransportConfig();
		
		// Test conditions to invalidate the proxy
		if (listener == null) {
			throw new IllegalArgumentException("IProxyListener listener must be provided to instantiate SmartDeviceLinkProxy object.");
		}
		if (_advancedLifecycleManagementEnabled) {
			if (_applicationName == null ) {
				throw new IllegalArgumentException("To use SmartDeviceLinkProxyALM, an application name, appName, must be provided");
			}
			if (_applicationName.length() < 1 || _applicationName.length() > 100) {
				throw new IllegalArgumentException("A provided application name, appName, must be between 1 and 100 characters in length.");
			}
			if (_isMediaApp == null) {
				throw new IllegalArgumentException("isMediaApp must not be null when using SmartDeviceLinkProxyALM.");
			}
		}
		
		_proxyListener = listener;
		
		// Get information from SmartDeviceLinkProxyConfigurationResources
		TelephonyManager telephonyManager = null;
		if (SmartDeviceLinkProxyConfigurationResources != null) {
			telephonyManager = SmartDeviceLinkProxyConfigurationResources.getTelephonyManager();
		} 
		
		// Use the telephonyManager to get and log phone info
		if (telephonyManager != null) {
			// Following is not quite thread-safe (because m_traceLogger could test null twice),
			// so we need to fix this, but vulnerability (i.e. two instances of listener) is
			// likely harmless.
			if (_traceDeviceInterrogator == null) {
				_traceDeviceInterrogator = new TraceDeviceInfo(SmartDeviceLinkProxyConfigurationResources.getTelephonyManager());
			} // end-if
		} // end-if
		
		// Setup Internal ProxyMessage Dispatcher
		synchronized(INTERNAL_MESSAGE_QUEUE_THREAD_LOCK) {
			// Ensure internalProxyMessageDispatcher is null
			if (_internalProxyMessageDispatcher != null) {
				_internalProxyMessageDispatcher.dispose();
				_internalProxyMessageDispatcher = null;
			}
			
			_internalProxyMessageDispatcher = new ProxyMessageDispatcher<InternalProxyMessage>("INTERNAL_MESSAGE_DISPATCHER",
					new InternalProxyMessageComparitor(),
					new IDispatchingStrategy<InternalProxyMessage>() {

						@Override
						public void dispatch(InternalProxyMessage message) {
							dispatchInternalMessage((InternalProxyMessage)message);
						}
	
						@Override
						public void handleDispatchingError(String info, Exception ex) {
							handleErrorsFromInternalMessageDispatcher(info, ex);
						}
	
						@Override
						public void handleQueueingError(String info, Exception ex) {
							handleErrorsFromInternalMessageDispatcher(info, ex);
						}			
			});
		}
		
		// Setup Incoming ProxyMessage Dispatcher
		synchronized(INCOMING_MESSAGE_QUEUE_THREAD_LOCK) {
			// Ensure incomingProxyMessageDispatcher is null
			if (_incomingProxyMessageDispatcher != null) {
				_incomingProxyMessageDispatcher.dispose();
				_incomingProxyMessageDispatcher = null;
			}
			
			_incomingProxyMessageDispatcher = new ProxyMessageDispatcher<ProtocolMessage>("INCOMING_MESSAGE_DISPATCHER",
					new IncomingProtocolMessageComparitor(),
					new IDispatchingStrategy<ProtocolMessage>() {
						@Override
						public void dispatch(ProtocolMessage message) {
							dispatchIncomingMessage((ProtocolMessage)message);
						}
	
						@Override
						public void handleDispatchingError(String info, Exception ex) {
							handleErrorsFromIncomingMessageDispatcher(info, ex);
						}
	
						@Override
						public void handleQueueingError(String info, Exception ex) {
							handleErrorsFromIncomingMessageDispatcher(info, ex);
						}			
			});
		}
		
		// Setup Outgoing ProxyMessage Dispatcher
		synchronized(OUTGOING_MESSAGE_QUEUE_THREAD_LOCK) {
			// Ensure outgoingProxyMessageDispatcher is null
			if (_outgoingProxyMessageDispatcher != null) {
				_outgoingProxyMessageDispatcher.dispose();
				_outgoingProxyMessageDispatcher = null;
			}
			
			_outgoingProxyMessageDispatcher = new ProxyMessageDispatcher<ProtocolMessage>("OUTGOING_MESSAGE_DISPATCHER",
					new OutgoingProtocolMessageComparitor(),
					new IDispatchingStrategy<ProtocolMessage>() {
						@Override
						public void dispatch(ProtocolMessage message) {
							dispatchOutgoingMessage((ProtocolMessage)message);
						}
	
						@Override
						public void handleDispatchingError(String info, Exception ex) {
							handleErrorsFromOutgoingMessageDispatcher(info, ex);
						}
	
						@Override
						public void handleQueueingError(String info, Exception ex) {
							handleErrorsFromOutgoingMessageDispatcher(info, ex);
						}
			});
		}
		
		// Initialize the proxy
		try {
			initializeProxy();
		} catch (SmartDeviceLinkException e) {
			// Couldn't initialize the proxy 
			// Dispose threads and then rethrow exception
			
			if (_internalProxyMessageDispatcher != null) {
				_internalProxyMessageDispatcher.dispose();
				_internalProxyMessageDispatcher = null;
			}
			if (_incomingProxyMessageDispatcher != null) {
				_incomingProxyMessageDispatcher.dispose();
				_incomingProxyMessageDispatcher = null;
			}
			if (_outgoingProxyMessageDispatcher != null) {
				_outgoingProxyMessageDispatcher.dispose();
				_outgoingProxyMessageDispatcher = null;
			}
			throw e;
		} 
		
		// Trace that ctor has fired
		SyncTrace.logProxyEvent("SmartDeviceLinkProxy Created, instanceID=" + this.toString(), SMARTDEVICELINK_LIB_TRACE_KEY);
	}

	// Test correlationID
	private boolean isCorrelationIDProtected(Integer correlationID) {
		if (correlationID != null && 
				(REGISTER_APP_INTERFACE_CORRELATION_ID == correlationID
						|| UNREGISTER_APP_INTERFACE_CORRELATION_ID == correlationID)) {
			return true;
		}
		
		return false;
	}
	
	// Protected isConnected method to allow legacy proxy to poll isConnected state
	public Boolean getIsConnected() {
		return _SmartDeviceLinkConnection.getIsConnected();
	}
	
	
	// Function to initialize new proxy connection
	private void initializeProxy() throws SmartDeviceLinkException {		
		// Reset all of the flags and state variables
		_haveReceivedFirstFocusLevel = false;
		_haveReceivedFirstFocusLevelFull = false;
		_SmartDeviceLinkIntefaceAvailablity = SmartDeviceLinkInterfaceAvailability.SMARTDEVICELINK_INTERFACE_UNAVAILABLE;
		
		// Setup SmartDeviceLinkConnection
		synchronized(CONNECTION_REFERENCE_LOCK) {
			if (_SmartDeviceLinkConnection != null) {
				_SmartDeviceLinkConnection.closeConnection(_rpcSessionID);
				_SmartDeviceLinkConnection = null;
			}
			_SmartDeviceLinkConnection = new SmartDeviceLinkConnection(_interfaceBroker, _transportConfig);
		}
		
		synchronized(CONNECTION_REFERENCE_LOCK) {
			if (_SmartDeviceLinkConnection != null) {
				_SmartDeviceLinkConnection.startTransport();
			}
		}
	}
	
	// Public method to enable the siphon transport
	public static void enableSiphonDebug() {
		SiphonServer.enableSiphonServer();
	}
	
	// Public method to disable the Siphon Trace Server
	public static void disableSiphonDebug() {
		SiphonServer.disableSiphonServer();
	}	
	
	// Public method to enable the Debug Tool
	public static void enableDebugTool() {
		DebugTool.enableDebugTool();
	}
	
	// Public method to disable the Debug Tool
	public static void disableDebugTool() {
		DebugTool.disableDebugTool();
	}	

	@Deprecated
	public void close() throws SmartDeviceLinkException {
		dispose();
	}
	
	private void cleanProxy(SmartDeviceLinkDisconnectedReason disconnectedReason) throws SmartDeviceLinkException {
		try {
			
			// ALM Specific Cleanup
			if (_advancedLifecycleManagementEnabled) {
				_SmartDeviceLinkConnectionState = SmartDeviceLinkConnectionState.SMARTDEVICELINK_DISCONNECTED;
				
				firstTimeFull = true;
			
				// Should we wait for the interface to be unregistered?
				Boolean waitForInterfaceUnregistered = false;
				// Unregister app interface
				synchronized(CONNECTION_REFERENCE_LOCK) {
					if (_appInterfaceRegisterd == true && _SmartDeviceLinkConnection != null && _SmartDeviceLinkConnection.getIsConnected()) {
						waitForInterfaceUnregistered = true;
						unregisterAppInterfacePrivate(UNREGISTER_APP_INTERFACE_CORRELATION_ID);
					}
				}
				
				// Wait for the app interface to be unregistered
				if (waitForInterfaceUnregistered) {
					synchronized(APP_INTERFACE_REGISTERED_LOCK) {
						try {
							APP_INTERFACE_REGISTERED_LOCK.wait(1000);
						} catch (InterruptedException e) {
							// Do nothing
						}
					}
				}
			}
			
			// Clean up SmartDeviceLink Connection
			synchronized(CONNECTION_REFERENCE_LOCK) {
				if (_SmartDeviceLinkConnection != null) {
					_SmartDeviceLinkConnection.closeConnection(_rpcSessionID);
					_SmartDeviceLinkConnection = null;
				}
			}
		} catch (SmartDeviceLinkException e) {
			throw e;
		} finally {
			SyncTrace.logProxyEvent("SmartDeviceLinkProxy cleaned.", SMARTDEVICELINK_LIB_TRACE_KEY);
		}
	}
	
	/**
	 * Terminates the App's Interface Registration, closes the transport connection, ends the protocol session, and frees any resources used by the proxy.
	 */
	public void dispose() throws SmartDeviceLinkException
	{		
		if (_proxyDisposed) {
			throw new SmartDeviceLinkException("This object has been disposed, it is no long capable of executing methods.", SmartDeviceLinkExceptionCause.SMARTDEVICELINK_PROXY_DISPOSED);
		}
		
		_proxyDisposed = true;
		
		SyncTrace.logProxyEvent("Application called dispose() method.", SMARTDEVICELINK_LIB_TRACE_KEY);
		
		try{
			// Clean the proxy
			cleanProxy(SmartDeviceLinkDisconnectedReason.APPLICATION_REQUESTED_DISCONNECT);
		
			// Close IncomingProxyMessageDispatcher thread
			synchronized(INCOMING_MESSAGE_QUEUE_THREAD_LOCK) {
				if (_incomingProxyMessageDispatcher != null) {
					_incomingProxyMessageDispatcher.dispose();
					_incomingProxyMessageDispatcher = null;
				}
			}
			
			// Close OutgoingProxyMessageDispatcher thread
			synchronized(OUTGOING_MESSAGE_QUEUE_THREAD_LOCK) {
				if (_outgoingProxyMessageDispatcher != null) {
					_outgoingProxyMessageDispatcher.dispose();
					_outgoingProxyMessageDispatcher = null;
				}
			}
			
			// Close InternalProxyMessageDispatcher thread
			synchronized(INTERNAL_MESSAGE_QUEUE_THREAD_LOCK) {
				if (_internalProxyMessageDispatcher != null) {
					_internalProxyMessageDispatcher.dispose();
					_internalProxyMessageDispatcher = null;
				}
			}
			
			_traceDeviceInterrogator = null;
		} catch (SmartDeviceLinkException e) {
			throw e;
		} finally {
			SyncTrace.logProxyEvent("SmartDeviceLinkProxy disposed.", SMARTDEVICELINK_LIB_TRACE_KEY);
		}
	} // end-method

	// Method to cycle the proxy, only called in ALM
	protected void cycleProxy(SmartDeviceLinkDisconnectedReason disconnectedReason) {		
		try{
			cleanProxy(disconnectedReason);
			initializeProxy();	
			notifyProxyClosed("SmartDeviceLink Proxy Cycled", new SmartDeviceLinkException("SmartDeviceLink Proxy Cycled", SmartDeviceLinkExceptionCause.SMARTDEVICELINK_PROXY_CYCLED));
		} catch (SmartDeviceLinkException e) {
			switch(e.getSmartDeviceLinkExceptionCause()) {
			case BLUETOOTH_DISABLED:
				notifyProxyClosed("Bluetooth is disabled. Bluetooth must be enabled to connect to SmartDeviceLink. Reattempt a connection once Bluetooth is enabled.", 
						new SmartDeviceLinkException("Bluetooth is disabled. Bluetooth must be enabled to connect to SmartDeviceLink. Reattempt a connection once Bluetooth is enabled.", SmartDeviceLinkExceptionCause.BLUETOOTH_DISABLED));
				break;
			case BLUETOOTH_ADAPTER_NULL:
				notifyProxyClosed("Cannot locate a Bluetooth adapater. A SmartDeviceLink connection is impossible on this device until a Bluetooth adapter is added.", 
						new SmartDeviceLinkException("Cannot locate a Bluetooth adapater. A SmartDeviceLink connection is impossible on this device until a Bluetooth adapter is added.", SmartDeviceLinkExceptionCause.HEARTBEAT_PAST_DUE));
				break;
			default :
				notifyProxyClosed("Cycling the proxy failed.", e);
				break;
			}
		} catch (Exception e) { 
			notifyProxyClosed("Cycling the proxy failed.", e);
		}
	}

	
	
	/************* Functions used by the Message Dispatching Queues ****************/
	private void dispatchIncomingMessage(ProtocolMessage message) {
		try{
			// Dispatching logic
			if (message.getSessionType().equals(SessionType.RPC)) {
				try {
					if (_protocolVersion == 1) {
						if (message.getVersion() == 2) setProtocolVersion(message.getVersion());
					}
					
					Hashtable hash = new Hashtable();
					if (_protocolVersion == 2) {
						Hashtable hashTemp = new Hashtable();
						hashTemp.put(Names.correlationID, message.getCorrID());
						if (message.getJsonSize() > 0) {
							final Hashtable<String, Object> mhash = JsonRPCMarshaller.unmarshall(message.getData());
							hashTemp.put(Names.parameters, mhash);
						}
						FunctionID functionID = new FunctionID();
						hashTemp.put(Names.function_name, functionID.getFunctionName(message.getFunctionID()));
						if (message.getRPCType() == 0x00) {
							hash.put(Names.request, hashTemp);
						} else if (message.getRPCType() == 0x01) {
							hash.put(Names.response, hashTemp);
						} else if (message.getRPCType() == 0x02) {
							hash.put(Names.notification, hashTemp);
						}
						if (message.getBulkData() != null) hash.put(Names.bulkData, message.getBulkData());
					} else {
						final Hashtable<String, Object> mhash = JsonRPCMarshaller.unmarshall(message.getData());
						hash = mhash;
					}
					handleRPCMessage(hash);							
				} catch (final Exception excp) {
					DebugTool.logError("Failure handling protocol message: " + excp.toString(), excp);
					passErrorToProxyListener("Error handing incoming protocol message.", excp);
				} // end-catch
			} else {
				// Handle other protocol message types here
			}
		} catch (final Exception e) {
			// Pass error to application through listener 
			DebugTool.logError("Error handing proxy event.", e);
			passErrorToProxyListener("Error handing incoming protocol message.", e);
		}
	}
	
	private void setProtocolVersion(byte version) {
		this._protocolVersion = version;
	}

	private void handleErrorsFromIncomingMessageDispatcher(String info, Exception e) {
		passErrorToProxyListener(info, e);
	}
	
	private void dispatchOutgoingMessage(ProtocolMessage message) {
		synchronized(CONNECTION_REFERENCE_LOCK) {
			if (_SmartDeviceLinkConnection != null) {
				_SmartDeviceLinkConnection.sendMessage(message);
			}
		}		
		SyncTrace.logProxyEvent("SmartDeviceLinkProxy sending Protocol Message: " + message.toString(), SMARTDEVICELINK_LIB_TRACE_KEY);
	}
	
	private void handleErrorsFromOutgoingMessageDispatcher(String info, Exception e) {
		passErrorToProxyListener(info, e);
	}
	
	void dispatchInternalMessage(final InternalProxyMessage message) {
		try{
			if (message.getFunctionName().equals(Names.OnProxyError)) {
				final OnError msg = (OnError)message;			
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onError(msg.getInfo(), msg.getException());
						}
					});
				} else {
					_proxyListener.onError(msg.getInfo(), msg.getException());
				}
			/**************Start Legacy Specific Call-backs************/
			} else if (message.getFunctionName().equals(Names.OnProxyOpened)) {
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							((IProxyListener)_proxyListener).onProxyOpened();
						}
					});
				} else {
					((IProxyListener)_proxyListener).onProxyOpened();
				}
			} else if (message.getFunctionName().equals(Names.OnProxyClosed)) {
				final OnProxyClosed msg = (OnProxyClosed)message;
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onProxyClosed(msg.getInfo(), msg.getException());
						}
					});
				} else {
					_proxyListener.onProxyClosed(msg.getInfo(), msg.getException());
				}
			/****************End Legacy Specific Call-backs************/
			} else {
				// Diagnostics
				SyncTrace.logProxyEvent("Unknown RPC Message encountered. Check for an updated version of the SmartDeviceLink Proxy.", SMARTDEVICELINK_LIB_TRACE_KEY);
				DebugTool.logError("Unknown RPC Message encountered. Check for an updated version of the SmartDeviceLink Proxy.");
			}
			
		SyncTrace.logProxyEvent("Proxy fired callback: " + message.getFunctionName(), SMARTDEVICELINK_LIB_TRACE_KEY);
		} catch(final Exception e) {
			// Pass error to application through listener 
			DebugTool.logError("Error handing proxy event.", e);
			if (_callbackToUIThread) {
				// Run in UI thread
				_mainUIHandler.post(new Runnable() {
					@Override
					public void run() {
						_proxyListener.onError("Error handing proxy event.", e);
					}
				});
			} else {
				_proxyListener.onError("Error handing proxy event.", e);
			}
		}
	}
	
	private void handleErrorsFromInternalMessageDispatcher(String info, Exception e) {
		DebugTool.logError(info, e);
		// This error cannot be passed to the user, as it indicates an error
		// in the communication between the proxy and the application.
		
		DebugTool.logError("InternalMessageDispatcher failed.", e);
		
		// Note, this is the only place where the _proxyListener should be referenced aSmartDeviceLinkhronously,
		// with an error on the internalMessageDispatcher, we have no other reliable way of 
		// communicating with the application.
		notifyProxyClosed("Proxy callback dispatcher is down. Proxy instance is invalid.", e);
		_proxyListener.onError("Proxy callback dispatcher is down. Proxy instance is invalid.", e);
	}
	/************* END Functions used by the Message Dispatching Queues ****************/
	

	// Private sendPRCRequest method. All RPCRequests are funneled through this method after
		// error checking. 
	private void sendRPCRequestPrivate(RPCRequest request) throws SmartDeviceLinkException {
		SyncTrace.logRPCEvent(InterfaceActivityDirection.Transmit, request, SMARTDEVICELINK_LIB_TRACE_KEY);
		
		byte[] msgBytes = JsonRPCMarshaller.marshall(request, _protocolVersion);
		
		ProtocolMessage pm = new ProtocolMessage();
		pm.setData(msgBytes);
		pm.setSessionID(_rpcSessionID);
		pm.setMessageType(MessageType.RPC);
		pm.setSessionType(SessionType.RPC);
		FunctionID functionID = new FunctionID();
		pm.setFunctionID(functionID.getFunctionID(request.getFunctionName()));
		pm.setCorrID(request.getCorrelationID());
		
		// Queue this outgoing message
		synchronized(OUTGOING_MESSAGE_QUEUE_THREAD_LOCK) {
			if (_outgoingProxyMessageDispatcher != null) {
				_outgoingProxyMessageDispatcher.queueMessage(pm);
			}
		}
	}
	
	private void handleRPCMessage(Hashtable hash) {
		RPCMessage rpcMsg = new RPCMessage(hash);
		String functionName = rpcMsg.getFunctionName();
		String messageType = rpcMsg.getMessageType();
		
		if (messageType.equals(Names.response)) {			
			SyncTrace.logRPCEvent(InterfaceActivityDirection.Receive, new RPCResponse(rpcMsg), SMARTDEVICELINK_LIB_TRACE_KEY);

			// Check to ensure response is not from an internal message (reserved correlation ID)
			if (isCorrelationIDProtected((new RPCResponse(hash)).getCorrelationID())) {
				// This is a response generated from an internal message, it can be trapped here
				// The app should not receive a response for a request it did not send
				if ((new RPCResponse(hash)).getCorrelationID() == REGISTER_APP_INTERFACE_CORRELATION_ID 
						&& _advancedLifecycleManagementEnabled 
						&& functionName.equals(Names.RegisterAppInterface)) {
					final RegisterAppInterfaceResponse msg = new RegisterAppInterfaceResponse(hash);
					if (msg.getSuccess()) {
						_appInterfaceRegisterd = true;
					}
					
					_autoActivateIdReturned = msg.getAutoActivateID();
					_buttonCapabilities = msg.getButtonCapabilities();
					_displayCapabilities = msg.getDisplayCapabilities();
					_hmiZoneCapabilities = msg.getHmiZoneCapabilities();
					_speechCapabilities = msg.getSpeechCapabilities();
					_SmartDeviceLinkLanguage = msg.getLanguage();
					_SyncMsgVersion = msg.getSyncMsgVersion();
					_vrCapabilities = msg.getVrCapabilities();
					
					// Send onSmartDeviceLinkConnected message in ALM
					_SmartDeviceLinkConnectionState = SmartDeviceLinkConnectionState.SMARTDEVICELINK_CONNECTED;
					
					// If registerAppInterface failed, exit with OnProxyUnusable
					if (!msg.getSuccess()) {
						notifyProxyClosed("Unable to register app interface. Review values passed to the SmartDeviceLinkProxy constructor. RegisterAppInterface result code: ", 
								new SmartDeviceLinkException("Unable to register app interface. Review values passed to the SmartDeviceLinkProxy constructor. RegisterAppInterface result code: " + msg.getResultCode(), SmartDeviceLinkExceptionCause.SMARTDEVICELINK_REGISTRATION_ERROR));
					}
				}
				return;
			}
			
			if (functionName.equals(Names.RegisterAppInterface)) {
				final RegisterAppInterfaceResponse msg = new RegisterAppInterfaceResponse(hash);
				if (msg.getSuccess()) {
					_appInterfaceRegisterd = true;
				}
				
				_autoActivateIdReturned = msg.getAutoActivateID();
				_buttonCapabilities = msg.getButtonCapabilities();
				_displayCapabilities = msg.getDisplayCapabilities();
				_hmiZoneCapabilities = msg.getHmiZoneCapabilities();
				_speechCapabilities = msg.getSpeechCapabilities();
				_SmartDeviceLinkLanguage = msg.getLanguage();
				_SyncMsgVersion = msg.getSyncMsgVersion();
				_vrCapabilities = msg.getVrCapabilities();
				
				// RegisterAppInterface
				if (_advancedLifecycleManagementEnabled) {
					
					// Send onSmartDeviceLinkConnected message in ALM
					_SmartDeviceLinkConnectionState = SmartDeviceLinkConnectionState.SMARTDEVICELINK_CONNECTED;
					
					// If registerAppInterface failed, exit with OnProxyUnusable
					if (!msg.getSuccess()) {
						notifyProxyClosed("Unable to register app interface. Review values passed to the SmartDeviceLinkProxy constructor. RegisterAppInterface result code: ", 
								new SmartDeviceLinkException("Unable to register app interface. Review values passed to the SmartDeviceLinkProxy constructor. RegisterAppInterface result code: " + msg.getResultCode(), SmartDeviceLinkExceptionCause.SMARTDEVICELINK_REGISTRATION_ERROR));
					}
				} else {	
					if (_callbackToUIThread) {
						// Run in UI thread
						_mainUIHandler.post(new Runnable() {
							@Override
							public void run() {
								((IProxyListener)_proxyListener).onRegisterAppInterfaceResponse(msg);
							}
						});
					} else {
						((IProxyListener)_proxyListener).onRegisterAppInterfaceResponse(msg);							
					}
				}
			} else if (functionName.equals(Names.Speak)) {
				// SpeakResponse
				
				final SpeakResponse msg = new SpeakResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onSpeakResponse(msg);
						}
					});
				} else {
					_proxyListener.onSpeakResponse(msg);						
				}
			} else if (functionName.equals(Names.Alert)) {
				// AlertResponse
				
				final AlertResponse msg = new AlertResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onAlertResponse(msg);
						}
					});
				} else {
					_proxyListener.onAlertResponse(msg);						
				}
			} else if (functionName.equals(Names.Show)) {
				// ShowResponse
				
				final ShowResponse msg = new ShowResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onShowResponse((ShowResponse)msg);
						}
					});
				} else {
					_proxyListener.onShowResponse((ShowResponse)msg);						
				}
			} else if (functionName.equals(Names.AddCommand)) {
				// AddCommand
				
				final AddCommandResponse msg = new AddCommandResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onAddCommandResponse((AddCommandResponse)msg);
						}
					});
				} else {
					_proxyListener.onAddCommandResponse((AddCommandResponse)msg);					
				}
			} else if (functionName.equals(Names.DeleteCommand)) {
				// DeleteCommandResponse
				
				final DeleteCommandResponse msg = new DeleteCommandResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onDeleteCommandResponse((DeleteCommandResponse)msg);
						}
					});
				} else {
					_proxyListener.onDeleteCommandResponse((DeleteCommandResponse)msg);					
				}
			} else if (functionName.equals(Names.AddSubMenu)) {
				// AddSubMenu
				
				final AddSubMenuResponse msg = new AddSubMenuResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onAddSubMenuResponse((AddSubMenuResponse)msg);
						}
					});
				} else {
					_proxyListener.onAddSubMenuResponse((AddSubMenuResponse)msg);					
				}
			} else if (functionName.equals(Names.DeleteSubMenu)) {
				// DeleteSubMenu
				
				final DeleteSubMenuResponse msg = new DeleteSubMenuResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onDeleteSubMenuResponse((DeleteSubMenuResponse)msg);
						}
					});
				} else {
					_proxyListener.onDeleteSubMenuResponse((DeleteSubMenuResponse)msg);					
				}
			} else if (functionName.equals(Names.SubscribeButton)) {
				// SubscribeButton
				
				final SubscribeButtonResponse msg = new SubscribeButtonResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onSubscribeButtonResponse((SubscribeButtonResponse)msg);
						}
					});
				} else {
					_proxyListener.onSubscribeButtonResponse((SubscribeButtonResponse)msg);				
				}
			} else if (functionName.equals(Names.UnsubscribeButton)) {
				// UnsubscribeButton
				
				final UnsubscribeButtonResponse msg = new UnsubscribeButtonResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onUnsubscribeButtonResponse((UnsubscribeButtonResponse)msg);
						}
					});
				} else {
					_proxyListener.onUnsubscribeButtonResponse((UnsubscribeButtonResponse)msg);			
				}
			} else if (functionName.equals(Names.SetMediaClockTimer)) {
				// SetMediaClockTimer
				
				final SetMediaClockTimerResponse msg = new SetMediaClockTimerResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onSetMediaClockTimerResponse((SetMediaClockTimerResponse)msg);
						}
					});
				} else {
					_proxyListener.onSetMediaClockTimerResponse((SetMediaClockTimerResponse)msg);		
				}
			} else if (functionName.equals(Names.EncodedSyncPData)) {
				// EncodedSyncPData
				
				final EncodedSyncPDataResponse msg = new EncodedSyncPDataResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onEncodedSyncPDataResponse(msg); 
						}
					});
				} else {
					_proxyListener.onEncodedSyncPDataResponse(msg); 		
				}
			} else if (functionName.equals(Names.CreateInteractionChoiceSet)) {
				// CreateInteractionChoiceSet
				
				final CreateInteractionChoiceSetResponse msg = new CreateInteractionChoiceSetResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onCreateInteractionChoiceSetResponse((CreateInteractionChoiceSetResponse)msg);
						}
					});
				} else {
					_proxyListener.onCreateInteractionChoiceSetResponse((CreateInteractionChoiceSetResponse)msg);		
				}
			} else if (functionName.equals(Names.DeleteInteractionChoiceSet)) {
				// DeleteInteractionChoiceSet
				
				final DeleteInteractionChoiceSetResponse msg = new DeleteInteractionChoiceSetResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onDeleteInteractionChoiceSetResponse((DeleteInteractionChoiceSetResponse)msg);
						}
					});
				} else {
					_proxyListener.onDeleteInteractionChoiceSetResponse((DeleteInteractionChoiceSetResponse)msg);		
				}
			} else if (functionName.equals(Names.PerformInteraction)) {
				// PerformInteraction
				
				final PerformInteractionResponse msg = new PerformInteractionResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onPerformInteractionResponse((PerformInteractionResponse)msg);
						}
					});
				} else {
					_proxyListener.onPerformInteractionResponse((PerformInteractionResponse)msg);		
				}
			} else if (functionName.equals(Names.SetGlobalProperties)) {
				// SetGlobalPropertiesResponse (can also be Heartbeat)
				
				final SetGlobalPropertiesResponse msg = new SetGlobalPropertiesResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onSetGlobalPropertiesResponse((SetGlobalPropertiesResponse)msg);
						}
					});
				} else {
					_proxyListener.onSetGlobalPropertiesResponse((SetGlobalPropertiesResponse)msg);		
				}
			} else if (functionName.equals(Names.ResetGlobalProperties)) {
				// ResetGlobalProperties				
				
				final ResetGlobalPropertiesResponse msg = new ResetGlobalPropertiesResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onResetGlobalPropertiesResponse((ResetGlobalPropertiesResponse)msg);
						}
					});
				} else {
					_proxyListener.onResetGlobalPropertiesResponse((ResetGlobalPropertiesResponse)msg);		
				}
			} else if (functionName.equals(Names.UnregisterAppInterface)) {
				// UnregisterAppInterface
				
				_appInterfaceRegisterd = false;
				synchronized(APP_INTERFACE_REGISTERED_LOCK) {
					APP_INTERFACE_REGISTERED_LOCK.notify();
				}
				
				final UnregisterAppInterfaceResponse msg = new UnregisterAppInterfaceResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							((IProxyListener)_proxyListener).onUnregisterAppInterfaceResponse(msg);
						}
					});
				} else {
					((IProxyListener)_proxyListener).onUnregisterAppInterfaceResponse(msg);	
				}
				
				notifyProxyClosed("UnregisterAppInterfaceResponse", null);
			} else if (functionName.equals(Names.GenericResponse)) {
				// GenericResponse (Usually and error)
				final GenericResponse msg = new GenericResponse(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onGenericResponse((GenericResponse)msg);
						}
					});
				} else {
					_proxyListener.onGenericResponse((GenericResponse)msg);	
				}
			} else {
				if (_SyncMsgVersion != null) {
					DebugTool.logError("Unrecognized response Message: " + functionName.toString() + 
							"SmartDeviceLink Message Version = " + _SyncMsgVersion);
				} else {
					DebugTool.logError("Unrecognized response Message: " + functionName.toString());
				}
			} // end-if
		} else if (messageType.equals(Names.notification)) {
			SyncTrace.logRPCEvent(InterfaceActivityDirection.Receive, new RPCNotification(rpcMsg), SMARTDEVICELINK_LIB_TRACE_KEY);
			if (functionName.equals(Names.OnHMIStatus)) {
				// OnHMIStatus
				
				final OnHMIStatus msg = new OnHMIStatus(hash);
				msg.setFirstRun(new Boolean(firstTimeFull));
				if (msg.getHmiLevel() == HMILevel.HMI_FULL) firstTimeFull = false;
				
				if (msg.getHmiLevel() != _priorHmiLevel && msg.getAudioStreamingState() != _priorAudioStreamingState) {
					if (_callbackToUIThread) {
						// Run in UI thread
						_mainUIHandler.post(new Runnable() {
							@Override
							public void run() {
								_proxyListener.onOnHMIStatus((OnHMIStatus)msg);
							}
						});
					} else {
						_proxyListener.onOnHMIStatus((OnHMIStatus)msg);
					}
				}
			} else if (functionName.equals(Names.OnCommand)) {
				// OnCommand
				
				final OnCommand msg = new OnCommand(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onOnCommand((OnCommand)msg);
						}
					});
				} else {
					_proxyListener.onOnCommand((OnCommand)msg);
				}
			} else if (functionName.equals(Names.OnDriverDistraction)) {
				// OnDriverDistration
				
				final OnDriverDistraction msg = new OnDriverDistraction(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onOnDriverDistraction(msg);
						}
					});
				} else {
					_proxyListener.onOnDriverDistraction(msg);
				}
			} else if (functionName.equals(Names.OnEncodedSyncPData)) {
				// OnEncodedSyncPData
				
				final OnEncodedSyncPData msg = new OnEncodedSyncPData(hash);
					
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onOnEncodedSyncPData(msg);
						}
					});
				} else {
					_proxyListener.onOnEncodedSyncPData(msg);
				}
			} else if (functionName.equals(Names.OnPermissionsChange)) {
				//OnPermissionsChange
				
				final OnPermissionsChange msg = new OnPermissionsChange(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onOnPermissionsChange(msg);
						}
					});
				} else {
					_proxyListener.onOnPermissionsChange(msg);
				}
			} else if (functionName.equals(Names.OnTBTClientState)) {
				// OnTBTClientState
				
				final OnTBTClientState msg = new OnTBTClientState(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onOnTBTClientState(msg);
						}
					});
				} else {
					_proxyListener.onOnTBTClientState(msg);
				}
			} else if (functionName.equals(Names.OnButtonPress)) {
				// OnButtonPress
				
				final OnButtonPress msg = new OnButtonPress(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onOnButtonPress((OnButtonPress)msg);
						}
					});
				} else {
					_proxyListener.onOnButtonPress((OnButtonPress)msg);
				}
			} else if (functionName.equals(Names.OnButtonEvent)) {
				// OnButtonEvent
				
				final OnButtonEvent msg = new OnButtonEvent(hash);
				if (_callbackToUIThread) {
					// Run in UI thread
					_mainUIHandler.post(new Runnable() {
						@Override
						public void run() {
							_proxyListener.onOnButtonEvent((OnButtonEvent)msg);
						}
					});
				} else {
					_proxyListener.onOnButtonEvent((OnButtonEvent)msg);
				}
			} else if (functionName.equals(Names.OnAppInterfaceUnregistered)) {
				// OnAppInterfaceUnregistered
				
				_appInterfaceRegisterd = false;
				synchronized(APP_INTERFACE_REGISTERED_LOCK) {
					APP_INTERFACE_REGISTERED_LOCK.notify();
				}
				
				final OnAppInterfaceUnregistered msg = new OnAppInterfaceUnregistered(hash);
								
				if (_advancedLifecycleManagementEnabled) {
					// This requires the proxy to be cycled
					cycleProxy(SmartDeviceLinkDisconnectedReason.convertAppInterfaceUnregisteredReason(msg.getReason()));
				} else {
					if (_callbackToUIThread) {
						// Run in UI thread
						_mainUIHandler.post(new Runnable() {
							@Override
							public void run() {
								((IProxyListener)_proxyListener).onOnAppInterfaceUnregistered(msg);
							}
						});
					} else {
						((IProxyListener)_proxyListener).onOnAppInterfaceUnregistered(msg);
					}
					
					notifyProxyClosed("OnAppInterfaceUnregistered", null);
				}
			}
			else {
				if (_SyncMsgVersion != null) {
					DebugTool.logInfo("Unrecognized notification Message: " + functionName.toString() + 
							" connected to SmartDeviceLink using message version: " + _SyncMsgVersion.getMajorVersion() + "." + _SyncMsgVersion.getMinorVersion());
				} else {
					DebugTool.logInfo("Unrecognized notification Message: " + functionName.toString());
				}
			} // end-if
		} // end-if notification
		
		SyncTrace.logProxyEvent("Proxy received RPC Message: " + functionName, SMARTDEVICELINK_LIB_TRACE_KEY);
	}
	
	/**
	 * Takes an RPCRequest and sends it to SmartDeviceLink.  Responses are captured through callback on IProxyListener.  
	 * 
	 * @param msg
	 * @throws SmartDeviceLinkException
	 */
	public void sendRPCRequest(RPCRequest request) throws SmartDeviceLinkException {
		if (_proxyDisposed) {
			throw new SmartDeviceLinkException("This object has been disposed, it is no long capable of executing methods.", SmartDeviceLinkExceptionCause.SMARTDEVICELINK_PROXY_DISPOSED);
		}
		
		// Test if request is null
		if (request == null) {
			SyncTrace.logProxyEvent("Application called sendRPCRequest method with a null RPCRequest.", SMARTDEVICELINK_LIB_TRACE_KEY);
			throw new IllegalArgumentException("sendRPCRequest cannot be called with a null request.");
		}
		
		SyncTrace.logProxyEvent("Application called sendRPCRequest method for RPCRequest: ." + request.getFunctionName(), SMARTDEVICELINK_LIB_TRACE_KEY);
			
		// Test if SmartDeviceLinkConnection is null
		synchronized(CONNECTION_REFERENCE_LOCK) {
			if (_SmartDeviceLinkConnection == null || !_SmartDeviceLinkConnection.getIsConnected()) {
				SyncTrace.logProxyEvent("Application attempted to send and RPCRequest without a connected transport.", SMARTDEVICELINK_LIB_TRACE_KEY);
				throw new SmartDeviceLinkException("There is no valid connection to SmartDeviceLink. sendRPCRequest cannot be called until SmartDeviceLink has been connected.", SmartDeviceLinkExceptionCause.SMARTDEVICELINK_UNAVAILALBE);
			}
		}
		
		// Test for illegal correlation ID
		if (isCorrelationIDProtected(request.getCorrelationID())) {
			
			SyncTrace.logProxyEvent("Application attempted to use the reserved correlation ID, " + request.getCorrelationID(), SMARTDEVICELINK_LIB_TRACE_KEY);
			throw new SmartDeviceLinkException("Invalid correlation ID. The correlation ID, " + request.getCorrelationID()
					+ " , is a reserved correlation ID.", SmartDeviceLinkExceptionCause.RESERVED_CORRELATION_ID);
		}
		
		// Throw exception if RPCRequest is sent when SmartDeviceLink is unavailable 
		if (!_appInterfaceRegisterd && request.getFunctionName() != Names.RegisterAppInterface) {
			
			SyncTrace.logProxyEvent("Application attempted to send an RPCRequest (non-registerAppInterface), before the interface was registerd.", SMARTDEVICELINK_LIB_TRACE_KEY);
			throw new SmartDeviceLinkException("SmartDeviceLink is currently unavailable. RPC Requests cannot be sent.", SmartDeviceLinkExceptionCause.SMARTDEVICELINK_UNAVAILALBE);
		}
				
		if (_advancedLifecycleManagementEnabled) {
			if (		   request.getFunctionName() == Names.RegisterAppInterface
					|| request.getFunctionName() == Names.UnregisterAppInterface) {
				
				SyncTrace.logProxyEvent("Application attempted to send a RegisterAppInterface or UnregisterAppInterface while using ALM.", SMARTDEVICELINK_LIB_TRACE_KEY);
				throw new SmartDeviceLinkException("The RPCRequest, " + request.getFunctionName() + 
						", is unnallowed using the Advanced Lifecycle Management Model.", SmartDeviceLinkExceptionCause.INCORRECT_LIFECYCLE_MODEL);
			}
		}
		
		sendRPCRequestPrivate(request);
	} // end-method
	
	public void sendRPCRequest(RPCMessage request) throws SmartDeviceLinkException {
		sendRPCRequest((RPCRequest) request);
	}
	
	protected void notifyProxyClosed(final String info, final Exception e) {		
		SyncTrace.logProxyEvent("NotifyProxyClose", SMARTDEVICELINK_LIB_TRACE_KEY);
		
		OnProxyClosed message = new OnProxyClosed(info, e);
		queueInternalMessage(message);
	}

	private void passErrorToProxyListener(final String info, final Exception e) {
				
		OnError message = new OnError(info, e);
		queueInternalMessage(message);
	}
	
	private void startRPCProtocolSession(byte sessionID, String correlationID) {
		_rpcSessionID = sessionID;
		
		// Set Proxy Lifecyclek Available
		if (_advancedLifecycleManagementEnabled) {
			
			try {
				registerAppInterfacePrivate(
						_SyncMsgVersionRequest,
						_applicationName,
						_ngnMediaScreenAppName,
						_vrSynonyms,
						_isMediaApp, 
						_SmartDeviceLinkLanguageDesired,
						_autoActivateIdDesired,
						REGISTER_APP_INTERFACE_CORRELATION_ID);
				
			} catch (Exception e) {
				notifyProxyClosed("Failed to register application interface with SmartDeviceLink. Check parameter values given to SmartDeviceLinkProxy constructor.", e);
			}
		} else {
			InternalProxyMessage message = new InternalProxyMessage(Names.OnProxyOpened);
			queueInternalMessage(message);
		}
	}
	
	// Queue internal callback message
	private void queueInternalMessage(InternalProxyMessage message) {
		synchronized(INTERNAL_MESSAGE_QUEUE_THREAD_LOCK) {
			if (_internalProxyMessageDispatcher != null) {
				_internalProxyMessageDispatcher.queueMessage(message);
			}
		}
	}
	
	// Queue incoming ProtocolMessage
	private void queueIncomingMessage(ProtocolMessage message) {
		synchronized(INCOMING_MESSAGE_QUEUE_THREAD_LOCK) {
			if (_incomingProxyMessageDispatcher != null) {
				_incomingProxyMessageDispatcher.queueMessage(message);
			}
		}
	}

	/******************** Public Helper Methods *************************/
	
	/**
	 *Sends an AddCommand RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 *
	 *@param commandID
	 *@param menuText
	 *@param parentID
	 *@param position
	 *@param vrCommands
	 *@param correlationID
	 *@throws SmartDeviceLinkException
	 */
	public void addCommand(Integer commandID,
			String menuText, Integer parentID, Integer position,
			Vector<String> vrCommands, Integer correlationID) 
			throws SmartDeviceLinkException {
		
		AddCommand msg = RPCRequestFactory.buildAddCommand(commandID, menuText, parentID, position,
			vrCommands, correlationID);
		
		sendRPCRequest(msg);
	}
	
	/**
	 *Sends an AddCommand RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 *
	 *@param commandID
	 *@param menuText
	 *@param position
	 *@param vrCommands
	 *@param correlationID
	 *@throws SmartDeviceLinkException
	 */
	public void addCommand(Integer commandID,
			String menuText, Integer position,
			Vector<String> vrCommands, Integer correlationID) 
			throws SmartDeviceLinkException {
		
		addCommand(commandID, menuText, null, position, vrCommands, correlationID);
	}
	
	/**
	 *Sends an AddCommand RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 *
	 *@param commandID
	 *@param menuText
	 *@param position
	 *@param correlationID
	 *@throws SmartDeviceLinkException
	 */
	public void addCommand(Integer commandID,
			String menuText, Integer position,
			Integer correlationID) 
			throws SmartDeviceLinkException {
		
		addCommand(commandID, menuText, null, position, null, correlationID);
	}
	
	/**
	 *Sends an AddCommand RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 *
	 *@param commandID
	 *@param menuText
	 *@param correlationID
	 *@throws SmartDeviceLinkException
	 */
	public void addCommand(Integer commandID,
			String menuText, Integer correlationID) 
			throws SmartDeviceLinkException {
		
		addCommand(commandID, menuText, null, null, null, correlationID);
	}
	
	/**
	 * Sends an AddCommand RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param commandID
	 * @param menuText
	 * @param vrCommands
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void addCommand(Integer commandID,
			String menuText, Vector<String> vrCommands, Integer correlationID) 
			throws SmartDeviceLinkException {
		
		addCommand(commandID, menuText, null, null, vrCommands, correlationID);
	}
	
	/**
	 * Sends an AddCommand RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param commandID
	 * @param vrCommands
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void addCommand(Integer commandID,
			Vector<String> vrCommands, Integer correlationID) 
			throws SmartDeviceLinkException {
		
		addCommand(commandID, null, null, null, vrCommands, correlationID);
	}
	
	/**
	 * Sends an AddSubMenu RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param menuID
	 * @param menuName
	 * @param position
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void addSubMenu(Integer menuID, String menuName,
			Integer position, Integer correlationID) 
			throws SmartDeviceLinkException {
		
		AddSubMenu msg = RPCRequestFactory.buildAddSubMenu(menuID, menuName,
				position, correlationID);
		
		sendRPCRequest(msg);
	}
	
	/**
	 * Sends an AddSubMenu RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param menuID
	 * @param menuName
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void addSubMenu(Integer menuID, String menuName,
			Integer correlationID) throws SmartDeviceLinkException {
		
		addSubMenu(menuID, menuName, null, correlationID);
	}
	
	/**
	 * Sends an EncodedData RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param data
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void encodedSmartDeviceLinkPData(Vector<String> data, Integer correlationID) 
			throws SmartDeviceLinkException {
		
		EncodedSyncPData msg = RPCRequestFactory.buildEncodedSyncPData(data, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Sends an Alert RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param ttsText
	 * @param alertText1
	 * @param alertText2
	 * @param playTone
	 * @param duration
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void alert(String ttsText, String alertText1,
			String alertText2, Boolean playTone, Integer duration,
			Integer correlationID) throws SmartDeviceLinkException {

		Alert msg = RPCRequestFactory.buildAlert(ttsText, alertText1, alertText2, 
				playTone, duration, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Sends an Alert RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param ttsChunks
	 * @param alertText1
	 * @param alertText2
	 * @param playTone
	 * @param duration
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void alert(Vector<TTSChunk> ttsChunks,
			String alertText1, String alertText2, Boolean playTone,
			Integer duration, Integer correlationID) throws SmartDeviceLinkException {
		
		Alert msg = RPCRequestFactory.buildAlert(ttsChunks, alertText1, alertText2, playTone,
				duration, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Sends an Alert RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param ttsText
	 * @param playTone
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void alert(String ttsText, Boolean playTone,
			Integer correlationID) throws SmartDeviceLinkException {
		
		alert(ttsText, null, null, playTone, null, correlationID);
	}
	
	/**
	 * Sends an Alert RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param chunks
	 * @param playTone
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void alert(Vector<TTSChunk> chunks, Boolean playTone,
			Integer correlationID) throws SmartDeviceLinkException {
		
		alert(chunks, null, null, playTone, null, correlationID);
	}
	
	/**
	 * Sends an Alert RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param alertText1
	 * @param alertText2
	 * @param playTone
	 * @param duration
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void alert(String alertText1, String alertText2,
			Boolean playTone, Integer duration, Integer correlationID) 
			throws SmartDeviceLinkException {
		
		alert((Vector<TTSChunk>)null, alertText1, alertText2, playTone, duration, correlationID);
	}
	
	/**
	 * Sends a CreateInteractionChoiceSet RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param choiceSet
	 * @param interactionChoiceSetID
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void createInteractionChoiceSet(
			Vector<Choice> choiceSet, Integer interactionChoiceSetID,
			Integer correlationID) throws SmartDeviceLinkException {
		
		CreateInteractionChoiceSet msg = RPCRequestFactory.buildCreateInteractionChoiceSet(
				choiceSet, interactionChoiceSetID, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Sends a DeleteCommand RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param commandID
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void deleteCommand(Integer commandID,
			Integer correlationID) throws SmartDeviceLinkException {
		
		DeleteCommand msg = RPCRequestFactory.buildDeleteCommand(commandID, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Sends a DeleteInteractionChoiceSet RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param interactionChoiceSetID
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void deleteInteractionChoiceSet(
			Integer interactionChoiceSetID, Integer correlationID) 
			throws SmartDeviceLinkException {
		
		DeleteInteractionChoiceSet msg = RPCRequestFactory.buildDeleteInteractionChoiceSet(
				interactionChoiceSetID, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Sends a DeleteSubMenu RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param menuID
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void deleteSubMenu(Integer menuID,
			Integer correlationID) throws SmartDeviceLinkException {
		
		DeleteSubMenu msg = RPCRequestFactory.buildDeleteSubMenu(menuID, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Sends a PerformInteraction RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param initPrompt
	 * @param displayText
	 * @param interactionChoiceSetID
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void performInteraction(String initPrompt,
			String displayText, Integer interactionChoiceSetID,
			Integer correlationID) throws SmartDeviceLinkException {
		
		PerformInteraction msg = RPCRequestFactory.buildPerformInteraction(initPrompt,
				displayText, interactionChoiceSetID, correlationID);
		
		sendRPCRequest(msg);
	}
	
	/**
	 * Sends a PerformInteraction RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param initPrompt
	 * @param displayText
	 * @param interactionChoiceSetID
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void performInteraction(String initPrompt,
			String displayText, Integer interactionChoiceSetID,
			String helpPrompt, String timeoutPrompt,
			InteractionMode interactionMode, Integer timeout,
			Integer correlationID) throws SmartDeviceLinkException {
		
		PerformInteraction msg = RPCRequestFactory.buildPerformInteraction(
				initPrompt, displayText, interactionChoiceSetID,
				helpPrompt, timeoutPrompt, interactionMode, 
				timeout, correlationID);
		
		sendRPCRequest(msg);
	}
	
	/**
	 * Sends a PerformInteraction RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param initPrompt
	 * @param displayText
	 * @param interactionChoiceSetIDList
	 * @param helpPrompt
	 * @param timeoutPrompt
	 * @param interactionMode
	 * @param timeout
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void performInteraction(String initPrompt,
			String displayText, Vector<Integer> interactionChoiceSetIDList,
			String helpPrompt, String timeoutPrompt,
			InteractionMode interactionMode, Integer timeout,
			Integer correlationID) throws SmartDeviceLinkException {
		
		PerformInteraction msg = RPCRequestFactory.buildPerformInteraction(initPrompt,
				displayText, interactionChoiceSetIDList,
				helpPrompt, timeoutPrompt, interactionMode, timeout,
				correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Sends a PerformInteraction RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param initChunks
	 * @param displayText
	 * @param interactionChoiceSetIDList
	 * @param helpChunks
	 * @param timeoutChunks
	 * @param interactionMode
	 * @param timeout
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void performInteraction(
			Vector<TTSChunk> initChunks, String displayText,
			Vector<Integer> interactionChoiceSetIDList,
			Vector<TTSChunk> helpChunks, Vector<TTSChunk> timeoutChunks,
			InteractionMode interactionMode, Integer timeout,
			Integer correlationID) throws SmartDeviceLinkException {
		
		PerformInteraction msg = RPCRequestFactory.buildPerformInteraction(
				initChunks, displayText, interactionChoiceSetIDList,
				helpChunks, timeoutChunks, interactionMode, timeout,
				correlationID);
		
		sendRPCRequest(msg);
	}
	
	// Protected registerAppInterface used to ensure only non-ALM applications call
	// reqisterAppInterface
	protected void registerAppInterfacePrivate(
			SyncMsgVersion SyncMsgVersion, String appName, String ngnMediaScreenAppName,
			Vector<String> vrSynonyms, Boolean isMediaApp, Language languageDesired, 
			String autoActivateID, Integer correlationID) 
			throws SmartDeviceLinkException {
		
		RegisterAppInterface msg = RPCRequestFactory.buildRegisterAppInterface(
				SyncMsgVersion, appName, ngnMediaScreenAppName, vrSynonyms, isMediaApp, 
				languageDesired, autoActivateID, correlationID);

		sendRPCRequestPrivate(msg);
	}
	
	/**
	 * Sends a SetGlobalProperties RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param helpPrompt
	 * @param timeoutPrompt
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void setGlobalProperties(
			String helpPrompt, String timeoutPrompt, Integer correlationID) 
		throws SmartDeviceLinkException {
		
		SetGlobalProperties req = RPCRequestFactory.buildSetGlobalProperties(helpPrompt, 
				timeoutPrompt, correlationID);
		
		sendRPCRequest(req);
	}
	
	/**
	 * Sends a SetGlobalProperties RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param helpChunks
	 * @param timeoutChunks
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void setGlobalProperties(
			Vector<TTSChunk> helpChunks, Vector<TTSChunk> timeoutChunks,
			Integer correlationID) throws SmartDeviceLinkException {
		
		SetGlobalProperties req = RPCRequestFactory.buildSetGlobalProperties(
				helpChunks, timeoutChunks, correlationID);

		sendRPCRequest(req);
	}
	
	public void resetGlobalProperties(Vector<GlobalProperty> properties,
			Integer correlationID) throws SmartDeviceLinkException {
		
		ResetGlobalProperties req = new ResetGlobalProperties();
		
		req.setCorrelationID(correlationID);
		req.setProperties(properties);
		
		sendRPCRequest(req);
	}
	                                                        
	
	/**
	 * Sends a SetMediaClockTimer RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param hours
	 * @param minutes
	 * @param seconds
	 * @param updateMode
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void setMediaClockTimer(Integer hours,
			Integer minutes, Integer seconds, UpdateMode updateMode,
			Integer correlationID) throws SmartDeviceLinkException {

		SetMediaClockTimer msg = RPCRequestFactory.buildSetMediaClockTimer(hours,
				minutes, seconds, updateMode, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Pauses the media clock. Responses are captured through callback on IProxyListener.
	 * 
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void pauseMediaClockTimer(Integer correlationID) 
			throws SmartDeviceLinkException {

		SetMediaClockTimer msg = RPCRequestFactory.buildSetMediaClockTimer(0,
				0, 0, UpdateMode.PAUSE, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Resumes the media clock. Responses are captured through callback on IProxyListener.
	 * 
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void resumeMediaClockTimer(Integer correlationID) 
			throws SmartDeviceLinkException {

		SetMediaClockTimer msg = RPCRequestFactory.buildSetMediaClockTimer(0,
				0, 0, UpdateMode.RESUME, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Clears the media clock. Responses are captured through callback on IProxyListener.
	 * 
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void clearMediaClockTimer(Integer correlationID) 
			throws SmartDeviceLinkException {

		Show msg = RPCRequestFactory.buildShow(null, null, null, "     ", null, null, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Sends a Show RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param mainText1
	 * @param mainText2
	 * @param statusBar
	 * @param mediaClock
	 * @param mediaTrack
	 * @param alignment
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void show(String mainText1, String mainText2,
			String statusBar, String mediaClock, String mediaTrack,
			TextAlignment alignment, Integer correlationID) 
			throws SmartDeviceLinkException {
		
		Show msg = RPCRequestFactory.buildShow(mainText1, mainText2,
				statusBar, mediaClock, mediaTrack,
				alignment, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Sends a Show RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param mainText1
	 * @param mainText2
	 * @param alignment
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void show(String mainText1, String mainText2,
			TextAlignment alignment, Integer correlationID) 
			throws SmartDeviceLinkException {
		
		show(mainText1, mainText2, null, null, null, alignment, correlationID);
	}
	
	/**
	 * Sends a Speak RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param ttsText
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void speak(String ttsText, Integer correlationID) 
			throws SmartDeviceLinkException {
		
		Speak msg = RPCRequestFactory.buildSpeak(TTSChunkFactory.createSimpleTTSChunks(ttsText),
				correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Sends a Speak RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param ttsChunks
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void speak(Vector<TTSChunk> ttsChunks,
			Integer correlationID) throws SmartDeviceLinkException {

		Speak msg = RPCRequestFactory.buildSpeak(ttsChunks, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Sends a SubscribeButton RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param buttonName
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void subscribeButton(ButtonName buttonName,
			Integer correlationID) throws SmartDeviceLinkException {

		SubscribeButton msg = RPCRequestFactory.buildSubscribeButton(buttonName,
				correlationID);

		sendRPCRequest(msg);
	}
	
	// Protected unregisterAppInterface used to ensure no non-ALM app calls
	// unregisterAppInterface.
	protected void unregisterAppInterfacePrivate(Integer correlationID) 
		throws SmartDeviceLinkException {

		UnregisterAppInterface msg = 
				RPCRequestFactory.buildUnregisterAppInterface(correlationID);
		
		sendRPCRequestPrivate(msg);
	}
	
	/**
	 * Sends an UnsubscribeButton RPCRequest to SmartDeviceLink. Responses are captured through callback on IProxyListener.
	 * 
	 * @param buttonName
	 * @param correlationID
	 * @throws SmartDeviceLinkException
	 */
	public void unsubscribeButton(ButtonName buttonName, 
			Integer correlationID) throws SmartDeviceLinkException {

		UnsubscribeButton msg = RPCRequestFactory.buildUnsubscribeButton(
				buttonName, correlationID);

		sendRPCRequest(msg);
	}
	
	/**
	 * Creates a choice to be added to a choiceset. Choice has both a voice and a visual menu component.
	 * 
	 * @param choiceID -Unique ID used to identify this choice (returned in callback).
	 * @param choiceMenuName -Text name displayed for this choice.
	 * @param choiceVrCommands -Vector of vrCommands used to select this choice by voice. Must contain
	 * 			at least one non-empty element.
	 * @return Choice created. 
	 * @throws SmartDeviceLinkException 
	 */
	public Choice createChoiceSetChoice(Integer choiceID, String choiceMenuName,
			Vector<String> choiceVrCommands) {		
		Choice returnChoice = new Choice();
		
		returnChoice.setChoiceID(choiceID);
		returnChoice.setMenuName(choiceMenuName);
		returnChoice.setVrCommands(choiceVrCommands);
		
		return returnChoice;
	}
	
	/******************** END Public Helper Methods *************************/

	public TransportType getCurrentTransportType() throws IllegalStateException {
		if (_SmartDeviceLinkConnection == null) {
			throw new IllegalStateException("Incorrect state of SmartDeviceLinkProxyBase: Calling for getCurrentTransportType() while connection is not initialized");
		}
			
		return _SmartDeviceLinkConnection.getCurrentTransportType();
	}
} // end-class
