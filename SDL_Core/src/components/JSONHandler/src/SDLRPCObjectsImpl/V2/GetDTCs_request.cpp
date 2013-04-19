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

#include "../include/JSONHandler/SDLRPCObjects/V2/GetDTCs_request.h"
#include "GetDTCs_requestMarshaller.h"
#include "../include/JSONHandler/SDLRPCObjects/V2/Marshaller.h"

#define PROTOCOL_VERSION	2


/*
  interface	Ford Sync RAPI
  version	2.0O
  date		2012-11-02
  generated at	Thu Jan 24 06:36:23 2013
  source stamp	Thu Jan 24 06:35:41 2013
  author	RC
*/

using namespace NsSmartDeviceLinkRPCV2;
GetDTCs_request& GetDTCs_request::operator =(const GetDTCs_request& c)
{
  ecuName= c.ecuName;
  encrypted= c.encrypted ? new bool(c.encrypted[0]) : 0;

  return *this;
}


GetDTCs_request::~GetDTCs_request(void)
{
  if(encrypted)
    delete encrypted;
}


GetDTCs_request::GetDTCs_request(const GetDTCs_request& c) : NsSmartDeviceLinkRPC::SDLRPCMessage(c)
{
  *this=c;
}


bool GetDTCs_request::checkIntegrity(void)
{
  return GetDTCs_requestMarshaller::checkIntegrity(*this);
}


GetDTCs_request::GetDTCs_request(void) : NsSmartDeviceLinkRPC::SDLRPCMessage(PROTOCOL_VERSION),
      encrypted(0)
{
}



bool GetDTCs_request::set_ecuName(unsigned int ecuName_)
{
  if(ecuName_>65535)  return false;
  ecuName=ecuName_;
  return true;
}

bool GetDTCs_request::set_encrypted(bool encrypted_)
{
  delete encrypted;
  encrypted=0;

  encrypted=new bool(encrypted_);
  return true;
}

void GetDTCs_request::reset_encrypted(void)
{
  if(encrypted)
    delete encrypted;
  encrypted=0;
}




unsigned int GetDTCs_request::get_ecuName(void) const
{
  return ecuName;
}

const bool* GetDTCs_request::get_encrypted(void) const 
{
  return encrypted;
}

