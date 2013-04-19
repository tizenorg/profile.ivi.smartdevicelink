//
// Copyright (c) 2013 Ford Motor Company
//
package com.smartdevicelink.trace.enums;

public enum Mod {
	  tran,
	  proto,
	  mar,
	  rpc,
	  app,
	  proxy;

	public static Mod valueForString(String value) {
		return valueOf(value);
	}
};
