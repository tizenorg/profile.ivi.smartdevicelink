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

#include "../include/JSONHandler/SDLRPCObjects/V2/ParameterPermissions.h"
#include "ParameterPermissionsMarshaller.h"

/*
  interface	Ford Sync RAPI
  version	2.0O
  date		2012-11-02
  generated at	Thu Jan 24 06:36:23 2013
  source stamp	Thu Jan 24 06:35:41 2013
  author	RC
*/



using namespace NsSmartDeviceLinkRPCV2;

ParameterPermissions::ParameterPermissions(const ParameterPermissions& c)
{
  *this=c;
}


bool ParameterPermissions::checkIntegrity(void)
{
  return ParameterPermissionsMarshaller::checkIntegrity(*this);
}


ParameterPermissions::ParameterPermissions(void)
{
}



bool ParameterPermissions::set_allowed(const std::vector<std::string>& allowed_)
{
  unsigned int i=allowed_.size();
  if(i>100 || i<0)  return false;
  while(i--)
  {
    if(allowed_[i].length()>100)  return false;
  }
  allowed=allowed_;
  return true;
}

bool ParameterPermissions::set_userDisallowed(const std::vector<std::string>& userDisallowed_)
{
  unsigned int i=userDisallowed_.size();
  if(i>100 || i<0)  return false;
  while(i--)
  {
  }
  userDisallowed=userDisallowed_;
  return true;
}




const std::vector<std::string>& ParameterPermissions::get_allowed(void) const 
{
  return allowed;
}


const std::vector<std::string>& ParameterPermissions::get_userDisallowed(void) const 
{
  return userDisallowed;
}


