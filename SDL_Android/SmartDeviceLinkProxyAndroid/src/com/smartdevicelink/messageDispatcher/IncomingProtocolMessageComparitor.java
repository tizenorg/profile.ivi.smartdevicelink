//
// Copyright (c) 2013 Ford Motor Company
//
package com.smartdevicelink.messageDispatcher;

import java.util.Comparator;

import com.smartdevicelink.protocol.ProtocolMessage;

public class IncomingProtocolMessageComparitor implements Comparator<ProtocolMessage> {

	@Override
	public int compare(ProtocolMessage arg0, ProtocolMessage arg1) {
		// Always return 0, turning the priority queue into a FIFO queue. 
		return 0;
	}
}
