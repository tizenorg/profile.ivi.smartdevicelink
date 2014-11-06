/**
 * \file appMain.cc
 * \brief SmartDeviceLink main application sources
 * Copyright (c) 2013, Ford Motor Company
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided with the
 * distribution.
 *
 * Neither the name of the Ford Motor Company nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <iostream>  // cpplint: Streams are highly discouraged.
#include <fstream>   // cpplint: Streams are highly discouraged.

// ----------------------------------------------------------------------------

#include "./life_cycle.h"

#include "utils/signals.h"
#include "utils/system.h"
#include "config_profile/profile.h"

#if defined(EXTENDED_MEDIA_MODE)
#include <gst/gst.h>
#endif

#include "media_manager/media_manager_impl.h"
// ----------------------------------------------------------------------------
// Third-Party includes
#include "networking.h"  // cpplint: Include the directory when naming .h files

// ----------------------------------------------------------------------------

#ifndef SDL_LOG4CXX_PROPERTIES_FILE
#define SDL_LOG4CXX_PROPERTIES_FILE "log4cxx.properties"
#endif

#ifndef SDL_HMI_LINK_FILE
#define SDL_HMI_LINK_FILE "hmi_link"
#endif

#ifndef SDL_HMI_BROWSER_PATH
#define SDL_HMI_BROWSER_PATH "/usr/bin/chromium-browser"
#define SDL_HMI_BROWSER_ARG0 "chromium-browser"
#define SDL_HMI_BROWSER_ARG1 "--auth-schemes=basic,digest,ntlm"
#endif

CREATE_LOGGERPTR_GLOBAL(logger, "appMain")
namespace {

const std::string kBrowser = SDL_HMI_BROWSER_PATH;
const std::string kBrowserName = SDL_HMI_BROWSER_ARG0;

#ifdef SDL_HMI_BROWSER_ARG1
const std::string kBrowserParams = SDL_HMI_BROWSER_ARG1;
#endif

const std::string kLocalHostAddress = "127.0.0.1";
const std::string kApplicationVersion = "SDL_RB_B3.5";

#ifdef WEB_HMI
/**
 * Initialize HTML based HMI.
 * @return true if success otherwise false.
 */
bool InitHmi() {

struct stat sb;
if (stat(SDL_HMI_LINK_FILE, &sb) == -1) {
  LOG4CXX_FATAL(logger, "File with HMI link doesn't exist!");
  return false;
}

std::ifstream file_str;
file_str.open(SDL_HMI_LINK_FILE);

if (!file_str.is_open()) {
  LOG4CXX_FATAL(logger, "File with HMI link was not opened!");
  return false;
}

file_str.seekg(0, std::ios::end);
int32_t length = file_str.tellg();
file_str.seekg(0, std::ios::beg);

std::string hmi_link;
std::getline(file_str, hmi_link);


LOG4CXX_INFO(logger,
             "Input string:" << hmi_link << " length = " << hmi_link.size());
file_str.close();

if (stat(hmi_link.c_str(), &sb) == -1) {
  LOG4CXX_INFO(logger, "HMI index.html doesn't exist!");
  // The hmi_link file in Tizen contains the Crosswalk application ID,
  // not a top-level HMI web page such as index.html, since we're
  // launching the HMI through xwalk-launcher.  Ignore the fact that
  // such a file doesn't exist.
  //
  // return false;
}
  return utils::System(kBrowser, kBrowserName)
#ifdef SDL_HMI_BROWSER_ARG1
      .Add(kBrowserParams)
#endif
      .Add(hmi_link).Execute();
}
#endif  // WEB_HMI

#ifdef QT_HMI
/**
 * Initialize HTML based HMI.
 * @return true if success otherwise false.
 */
bool InitHmi() {
  std::string kStartHmi = "./start_hmi.sh";
  struct stat sb;
  if (stat(kStartHmi.c_str(), &sb) == -1) {
    LOG4CXX_FATAL(logger, "HMI start script doesn't exist!");
    return false;
  }

  return utils::System(kStartHmi).Execute();
}
#endif  // QT_HMI

}

/**
 * \brief Entry point of the program.
 * \param argc number of argument
 * \param argv array of arguments
 * \return EXIT_SUCCESS or EXIT_FAILURE
 */
int32_t main(int32_t argc, char** argv) {

  // --------------------------------------------------------------------------
  // Logger initialization
  INIT_LOGGER(SDL_LOG4CXX_PROPERTIES_FILE);

  threads::Thread::SetNameForId(threads::Thread::CurrentId(), "MainThread");

  LOG4CXX_INFO(logger, "Application started!");
  LOG4CXX_INFO(logger, "Application version " << kApplicationVersion);

  // Initialize gstreamer. Needed to activate debug from the command line.
#if defined(EXTENDED_MEDIA_MODE)
  gst_init(&argc, &argv);
#endif

  // --------------------------------------------------------------------------
  // Components initialization
  if ((argc > 1)&&(0 != argv)) {
      profile::Profile::instance()->config_file_name(argv[1]);
  } else {
      profile::Profile::instance()->config_file_name(SDL_CONFIG_FILE);
  }

#ifdef __QNX__
  if (!profile::Profile::instance()->policy_turn_off()) {
    if (!utils::System("./init_policy.sh").Execute(true)) {
      LOG4CXX_ERROR(logger, "Failed initialization of policy database");
      DEINIT_LOGGER();
      exit(EXIT_FAILURE);
    }
  }
#endif  // __QNX__

  main_namespace::LifeCycle::instance()->StartComponents();

  // --------------------------------------------------------------------------
  // Third-Party components initialization.

  if (!main_namespace::LifeCycle::instance()->InitMessageSystem()) {
    main_namespace::LifeCycle::instance()->StopComponents();
    DEINIT_LOGGER();
    exit(EXIT_FAILURE);
  }
  LOG4CXX_INFO(logger, "InitMessageBroker successful");

  if (profile::Profile::instance()->launch_hmi()) {
    if (profile::Profile::instance()->server_address() == kLocalHostAddress) {
      LOG4CXX_INFO(logger, "Start HMI on localhost");

#ifndef NO_HMI
      if (!InitHmi()) {
        main_namespace::LifeCycle::instance()->StopComponents();
        DEINIT_LOGGER();
        exit(EXIT_FAILURE);
      }
      LOG4CXX_INFO(logger, "InitHmi successful");
#endif // #ifndef NO_HMI
    }
  }
  // --------------------------------------------------------------------------

  utils::SubscribeToTerminateSignal(
    &main_namespace::LifeCycle::StopComponentsOnSignal);

  pause();
}
