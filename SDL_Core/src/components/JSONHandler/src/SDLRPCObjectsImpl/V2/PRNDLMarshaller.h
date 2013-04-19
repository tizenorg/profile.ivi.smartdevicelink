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

#ifndef NSSMARTDEVICELINKRPCV2_PRNDLMARSHALLER_INCLUDE
#define NSSMARTDEVICELINKRPCV2_PRNDLMARSHALLER_INCLUDE

#include <string>
#include <json/json.h>

#include "PerfectHashTable.h"

#include "../include/JSONHandler/SDLRPCObjects/V2/PRNDL.h"


/*
  interface	Ford Sync RAPI
  version	2.0O
  date		2012-11-02
  generated at	Thu Jan 24 06:36:23 2013
  source stamp	Thu Jan 24 06:35:41 2013
  author	RC
*/

namespace NsSmartDeviceLinkRPCV2
{

//! marshalling class for PRNDL

  class PRNDLMarshaller
  {
  public:
  
    static std::string toName(const PRNDL& e) 	{ return getName(e.mInternal) ?: ""; }
  
    static bool fromName(PRNDL& e,const std::string& s)
    { 
      return (e.mInternal=getIndex(s.c_str()))!=PRNDL::INVALID_ENUM;
    }
  
    static bool checkIntegrity(PRNDL& e)		{ return e.mInternal!=PRNDL::INVALID_ENUM; } 
    static bool checkIntegrityConst(const PRNDL& e)	{ return e.mInternal!=PRNDL::INVALID_ENUM; } 
  
    static bool fromString(const std::string& s,PRNDL& e);
    static const std::string toString(const PRNDL& e);
  
    static bool fromJSON(const Json::Value& s,PRNDL& e);
    static Json::Value toJSON(const PRNDL& e);
  
    static const char* getName(PRNDL::PRNDLInternal e)
    {
       return (e>=0 && e<14) ? mHashTable[e].name : NULL;
    }
  
    static const PRNDL::PRNDLInternal getIndex(const char* s);
  
    static const PerfectHashTable mHashTable[14];
  };
  
}

#endif
