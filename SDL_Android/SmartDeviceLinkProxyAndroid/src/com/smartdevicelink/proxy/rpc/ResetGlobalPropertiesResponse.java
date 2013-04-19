//
// Copyright (c) 2013 Ford Motor Company
//
package com.smartdevicelink.proxy.rpc;

import java.util.Hashtable;

import com.smartdevicelink.proxy.RPCResponse;

public class ResetGlobalPropertiesResponse extends RPCResponse {

    public ResetGlobalPropertiesResponse() {
        super("ResetGlobalProperties");
    }
    public ResetGlobalPropertiesResponse(Hashtable hash) {
        super(hash);
    }
}
