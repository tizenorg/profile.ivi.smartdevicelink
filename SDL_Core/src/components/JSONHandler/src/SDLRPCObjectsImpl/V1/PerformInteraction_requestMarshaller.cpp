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

#include "../include/JSONHandler/SDLRPCObjects/V1/PerformInteraction_request.h"
#include "InteractionModeMarshaller.h"
#include "TTSChunkMarshaller.h"

#include "PerformInteraction_requestMarshaller.h"


/*
  interface	Ford Sync RAPI
  version	1.2
  date		2011-05-17
  generated at	Thu Jan 24 06:36:21 2013
  source stamp	Thu Jan 24 06:35:34 2013
  author	RC
*/

using namespace NsSmartDeviceLinkRPC;


bool PerformInteraction_requestMarshaller::checkIntegrity(PerformInteraction_request& s)
{
  return checkIntegrityConst(s);
}


bool PerformInteraction_requestMarshaller::fromString(const std::string& s,PerformInteraction_request& e)
{
  try
  {
    Json::Reader reader;
    Json::Value json;
    if(!reader.parse(s,json,false))  return false;
    if(!fromJSON(json,e))  return false;
  }
  catch(...)
  {
    return false;
  }
  return true;
}


const std::string PerformInteraction_requestMarshaller::toString(const PerformInteraction_request& e)
{
  Json::FastWriter writer;
  return checkIntegrityConst(e) ? writer.write(toJSON(e)) : "";
}


bool PerformInteraction_requestMarshaller::checkIntegrityConst(const PerformInteraction_request& s)
{
  if(s.initialText.length()>500)  return false;
  {
    unsigned int i=s.initialPrompt.size();
    if(i>100 || i<1)  return false;
    while(i--)
    {
    if(!TTSChunkMarshaller::checkIntegrityConst(s.initialPrompt[i]))   return false;
    }
  }
  if(!InteractionModeMarshaller::checkIntegrityConst(s.interactionMode))  return false;
  {
    unsigned int i=s.interactionChoiceSetIDList.size();
    if(i>100 || i<1)  return false;
    while(i--)
    {
      if(s.interactionChoiceSetIDList[i]>2000000000)  return false;
    }
  }
  if(s.helpPrompt)
  {
    unsigned int i=s.helpPrompt[0].size();
    if(i>100 || i<1)  return false;
    while(i--)
    {
    if(!TTSChunkMarshaller::checkIntegrityConst(s.helpPrompt[0][i]))   return false;
    }
  }
  if(s.timeoutPrompt)
  {
    unsigned int i=s.timeoutPrompt[0].size();
    if(i>100 || i<1)  return false;
    while(i--)
    {
    if(!TTSChunkMarshaller::checkIntegrityConst(s.timeoutPrompt[0][i]))   return false;
    }
  }
  if(s.timeout && *s.timeout>100000)  return false;
  if(s.timeout && *s.timeout<5000)  return false;
  return true;
}

Json::Value PerformInteraction_requestMarshaller::toJSON(const PerformInteraction_request& e)
{
  Json::Value json(Json::objectValue);
  if(!checkIntegrityConst(e))
    return Json::Value(Json::nullValue);

  json["request"]=Json::Value(Json::objectValue);
  json["request"]["name"]=Json::Value("PerformInteraction");
  json["request"]["correlationID"]=Json::Value(e.getCorrelationID());

  Json::Value j=Json::Value(Json::objectValue);

  j["initialText"]=Json::Value(e.initialText);

  j["initialPrompt"]=Json::Value(Json::arrayValue);
  j["initialPrompt"].resize(e.initialPrompt.size());
  for(unsigned int i=0;i<e.initialPrompt.size();i++)
    j["initialPrompt"][i]=TTSChunkMarshaller::toJSON(e.initialPrompt[i]);

  j["interactionMode"]=InteractionModeMarshaller::toJSON(e.interactionMode);

  j["interactionChoiceSetIDList"]=Json::Value(Json::arrayValue);
  j["interactionChoiceSetIDList"].resize(e.interactionChoiceSetIDList.size());
  for(unsigned int i=0;i<e.interactionChoiceSetIDList.size();i++)
    j["interactionChoiceSetIDList"][i]=Json::Value(e.interactionChoiceSetIDList[i]);

  if(e.helpPrompt)
  {
    unsigned int sz=e.helpPrompt->size();
    j["helpPrompt"]=Json::Value(Json::arrayValue);
    j["helpPrompt"].resize(sz);
    for(unsigned int i=0;i<sz;i++)
      j["helpPrompt"][i]=TTSChunkMarshaller::toJSON(e.helpPrompt[0][i]);
  }

  if(e.timeoutPrompt)
  {
    unsigned int sz=e.timeoutPrompt->size();
    j["timeoutPrompt"]=Json::Value(Json::arrayValue);
    j["timeoutPrompt"].resize(sz);
    for(unsigned int i=0;i<sz;i++)
      j["timeoutPrompt"][i]=TTSChunkMarshaller::toJSON(e.timeoutPrompt[0][i]);
  }

  if(e.timeout)
    j["timeout"]=Json::Value(*e.timeout);

  json["request"]["parameters"]=j;
  return json;
}


bool PerformInteraction_requestMarshaller::fromJSON(const Json::Value& js,PerformInteraction_request& c)
{
  if(c.helpPrompt)  delete c.helpPrompt;
  c.helpPrompt=0;

  if(c.timeoutPrompt)  delete c.timeoutPrompt;
  c.timeoutPrompt=0;

  if(c.timeout)  delete c.timeout;
  c.timeout=0;

  try
  {
    if(!js.isObject())  return false;

    if(!js.isMember("request"))  return false;

    if(!js["request"].isObject())  return false;
    const Json::Value& j2=js["request"];

    if(!j2.isMember("name") || !j2["name"].isString() || j2["name"].asString().compare("PerformInteraction"))  return false;
    if(!j2.isMember("correlationID") || !j2["correlationID"].isInt())  return false;
    c.setCorrelationID(j2["correlationID"].asInt());

    if(!j2.isMember("parameters"))  return false;
    const Json::Value& json=j2["parameters"];
    if(!json.isObject())  return false;
    if(!json.isMember("initialText"))  return false;
    {
      const Json::Value& j=json["initialText"];
      if(!j.isString())  return false;
      c.initialText=j.asString();
    }
    if(!json.isMember("initialPrompt"))  return false;
    {
      const Json::Value& j=json["initialPrompt"];
      if(!j.isArray())  return false;
      c.initialPrompt.resize(j.size());
      for(unsigned int i=0;i<j.size();i++)
        {
          TTSChunk t;
          if(!TTSChunkMarshaller::fromJSON(j[i],t))
            return false;
          c.initialPrompt[i]=t;
        }

    }
    if(!json.isMember("interactionMode"))  return false;
    {
      const Json::Value& j=json["interactionMode"];
      if(!InteractionModeMarshaller::fromJSON(j,c.interactionMode))
        return false;
    }
    if(!json.isMember("interactionChoiceSetIDList"))  return false;
    {
      const Json::Value& j=json["interactionChoiceSetIDList"];
      if(!j.isArray())  return false;
      c.interactionChoiceSetIDList.resize(j.size());
      for(unsigned int i=0;i<j.size();i++)
        if(!j[i].isInt())
          return false;
        else
          c.interactionChoiceSetIDList[i]=j[i].asInt();
    }
    if(json.isMember("helpPrompt"))
    {
      const Json::Value& j=json["helpPrompt"];
      if(!j.isArray())  return false;
      c.helpPrompt=new std::vector<TTSChunk>();
      c.helpPrompt->resize(j.size());
      for(unsigned int i=0;i<j.size();i++)
      {
        TTSChunk t;
        if(!TTSChunkMarshaller::fromJSON(j[i],t))
          return false;
        c.helpPrompt[0][i]=t;
      }

    }
    if(json.isMember("timeoutPrompt"))
    {
      const Json::Value& j=json["timeoutPrompt"];
      if(!j.isArray())  return false;
      c.timeoutPrompt=new std::vector<TTSChunk>();
      c.timeoutPrompt->resize(j.size());
      for(unsigned int i=0;i<j.size();i++)
      {
        TTSChunk t;
        if(!TTSChunkMarshaller::fromJSON(j[i],t))
          return false;
        c.timeoutPrompt[0][i]=t;
      }

    }
    if(json.isMember("timeout"))
    {
      const Json::Value& j=json["timeout"];
      if(!j.isInt())  return false;
      c.timeout=new unsigned int(j.asInt());
    }

  }
  catch(...)
  {
    return false;
  }
  return checkIntegrity(c);
}

