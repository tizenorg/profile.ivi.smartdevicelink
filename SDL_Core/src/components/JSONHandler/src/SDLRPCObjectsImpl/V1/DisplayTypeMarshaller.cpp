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

#include <cstring>
#include "../include/JSONHandler/SDLRPCObjects/V1/DisplayType.h"
#include "DisplayTypeMarshaller.h"
#include "DisplayTypeMarshaller.inc"


/*
  interface	Ford Sync RAPI
  version	1.2
  date		2011-05-17
  generated at	Thu Jan 24 06:36:21 2013
  source stamp	Thu Jan 24 06:35:34 2013
  author	RC
*/

using namespace NsSmartDeviceLinkRPC;


const DisplayType::DisplayTypeInternal DisplayTypeMarshaller::getIndex(const char* s)
{
  if(!s)
    return DisplayType::INVALID_ENUM;
  const struct PerfectHashTable* p=DisplayType_intHash::getPointer(s,strlen(s));
  return p ? static_cast<DisplayType::DisplayTypeInternal>(p->idx) : DisplayType::INVALID_ENUM;
}


bool DisplayTypeMarshaller::fromJSON(const Json::Value& s,DisplayType& e)
{
  e.mInternal=DisplayType::INVALID_ENUM;
  if(!s.isString())
    return false;

  e.mInternal=getIndex(s.asString().c_str());
  return (e.mInternal!=DisplayType::INVALID_ENUM);
}


Json::Value DisplayTypeMarshaller::toJSON(const DisplayType& e)
{
  if(e.mInternal==DisplayType::INVALID_ENUM) 
    return Json::Value(Json::nullValue);
  const char* s=getName(e.mInternal);
  return s ? Json::Value(s) : Json::Value(Json::nullValue);
}


bool DisplayTypeMarshaller::fromString(const std::string& s,DisplayType& e)
{
  e.mInternal=DisplayType::INVALID_ENUM;
  try
  {
    Json::Reader reader;
    Json::Value json;
    if(!reader.parse(s,json,false))  return false;
    if(fromJSON(json,e))  return true;
  }
  catch(...)
  {
    return false;
  }
  return false;
}

const std::string DisplayTypeMarshaller::toString(const DisplayType& e)
{
  Json::FastWriter writer;
  return e.mInternal==DisplayType::INVALID_ENUM ? "" : writer.write(toJSON(e));

}

const PerfectHashTable DisplayTypeMarshaller::mHashTable[9]=
{
  {"CID",0},
  {"TYPE2",1},
  {"TYPE5",2},
  {"NGN",3},
  {"GEN2_8_DMA",4},
  {"GEN2_6_DMA",5},
  {"MFD3",6},
  {"MFD4",7},
  {"MFD5",8}
};
