%define SDL_PKGID SDL0000001
%define SDL_APPID %{SDL_PKGID}.SmartDeviceLink

Name:          smartdevicelink
Summary:       GENIVI mobile device and HMI integration
Version:       3.5
Release:       1
Group:         Automotive/GENIVI
License:       BSD-3-Clause
URL:           http://projects.genivi.org/smartdevicelink/
Source:        %{name}-%{version}.tar.gz
Source1001:    %{name}.manifest
Source2001:    config.xml.in
Source2002:    manifest.json.in
BuildRequires: cmake
BuildRequires: pkgconfig(bluez)
BuildRequires: pkgconfig(gstreamer-1.0)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(liblog4cxx)
BuildRequires: pkgconfig(avahi-client)
BuildRequires: pkgconfig(libpulse-simple)
BuildRequires: pkgconfig(libtzplatform-config)
BuildRequires: pkgconfig(expat)
BuildRequires: pkgconfig(libcrypto)
BuildRequires: pkgconfig(libudev)
BuildRequires: pkgconfig(sqlite3)
BuildRequires: doxygen
BuildRequires: zip
Requires:      crosswalk

# Custom SDL-enabled HMIs should "Provide" this virtual package.
Requires:      smartdevicelink-hmi

%description
SmartDeviceLink is a project which intends to standardize and wrap the
many in-vehicle interfaces which may exist in the automotive
context. The end goal is to provide an expandable software framework
to both mobile application developers and automotive head unit
creators for the creation of brought-in applications that appear
integrated onto a head unit.

Many in-vehicle HMIs use different colors, templates, icons, fonts,
voice systems and input methods for their infotainment systems. With
SmartDeviceLink, a template-based approach is provided by the
automotive head unit and allows for different HMI frameworks to follow
a specific set of guidelines ensuring a consistent experience to a
developer. By leveraging this common API and a brought-in device,
automotive head units leverage the complete power of the brought-in
device using the APIs being executed on the mobile device.

%package sample-hmi
Summary:       Sample HMI
Provides:      smartdevicelink-hmi

%description sample-hmi
This package contains a sample/reference HMI.

%prep
%setup -q -n %{name}-%{version}
cp %{SOURCE1001} .

%build
mkdir html5_build
pushd html5_build
# For the Qt based HMI add -DHMI2=ON to the cmake command line flags.
%cmake .. -DEXTENDED_MEDIA_MODE=ON
make %{?_smp_mflags}
popd

%install

mkdir -p %{buildroot}%{_bindir} %{buildroot}%{_libdir}
mkdir -p %{buildroot}%{_datadir}/%{name}
mkdir -p %{buildroot}%{_sysconfdir}/%{name}

pushd html5_build/src/appMain
install -m 0755 smartDeviceLinkCore %{buildroot}%{_bindir}
install -m 0755 libPolicy.so %{buildroot}%{_libdir}
popd

pushd src/appMain
install -m 0644 audio.8bit.wav %{buildroot}%{_datadir}/%{name}
install -m 0644 \
    log4cxx.properties \
    hmi_capabilities.json \
    policy_table.json \
    sdl_preloaded_pt.json \
    sdl_update_pt.json \
    %{buildroot}%{_sysconfdir}/%{name}
sed -e 's,= \(.*\)\.json,= %{_sysconfdir}/%{name}/\1\.json,g' \
    -e 's,= \(audio.8bit.wav\),= %{_datadir}/%{name}/\1,' \
    smartDeviceLink.ini > \
    %{buildroot}%{_sysconfdir}/%{name}/smartDeviceLink.ini
popd

pushd html5_build/src/components
install -m 0755 rpc_base/librpc_base.so %{buildroot}%{_libdir}
install -m 0755 utils/libUtils.so %{buildroot}%{_libdir}
install -m 0755 connection_handler/libconnectionHandler.so %{buildroot}%{_libdir}
install -m 0755 request_watchdog/libRequestWatchdog.so %{buildroot}%{_libdir}
install -m 0755 resumption/libResumption.so %{buildroot}%{_libdir}
install -m 0755 smart_objects/libSmartObjects.so %{buildroot}%{_libdir}
install -m 0755 policy/src/policy/sqlite_wrapper/libdbms.so %{buildroot}%{_libdir}
install -m 0755 policy/src/policy/policy_table/table_struct/libpolicy_struct.so %{buildroot}%{_libdir}
install -m 0755 policy/src/policy/usage_statistics/libUsageStatistics.so %{buildroot}%{_libdir}
install -m 0755 policy/src/policy/libPolicy.so %{buildroot}%{_libdir}
install -m 0755 transport_manager/libTransportManager.so %{buildroot}%{_libdir}
install -m 0755 protocol_handler/libProtocolHandler.so %{buildroot}%{_libdir}
install -m 0755 hmi_message_handler/libHMIMessageHandler.so %{buildroot}%{_libdir}
install -m 0755 config_profile/libConfigProfile.so %{buildroot}%{_libdir}
install -m 0755 interfaces/libMOBILE_API.so %{buildroot}%{_libdir}
install -m 0755 interfaces/libv4_protocol_v1_2_no_extra.so %{buildroot}%{_libdir}
install -m 0755 interfaces/libHMI_API.so %{buildroot}%{_libdir}
install -m 0755 application_manager/libApplicationManager.so %{buildroot}%{_libdir}
install -m 0755 time_tester/libTimeTester.so %{buildroot}%{_libdir}
install -m 0755 media_manager/libMediaManager.so %{buildroot}%{_libdir}
install -m 0755 formatters/libformatters.so %{buildroot}%{_libdir}
popd

pushd html5_build/src/thirdPartyLibs
install -m 0755 encryption/libencryption.so %{buildroot}%{_libdir}
install -m 0755 MessageBroker/libMessageBrokerClient.so %{buildroot}%{_libdir}
install -m 0755 MessageBroker/libMessageBrokerServer.so %{buildroot}%{_libdir}
install -m 0755 MessageBroker/libMessageBroker.so %{buildroot}%{_libdir}
install -m 0755 libusbx-1.0.16/libLibusb-1.0.16.so %{buildroot}%{_libdir}
install -m 0755 jsoncpp/libjsoncpp.so %{buildroot}%{_libdir}
popd

# Sample HMI
# The SDL HMI will be launched with xwalk-launcher so package it as a
# Crosswalk widget.
mkdir -p %{buildroot}%{TZ_SYS_APP_PREINSTALL}
pushd %{dirname:%SOURCE2001}
sed -e 's/%%SDL_PKGID%%/%{SDL_PKGID}/' \
    -e 's/%%SDL_APPID%%/%{SDL_APPID}/' \
    -e 's/%%SDL_VERSION%%/%{version}/' %{SOURCE2001} > config.xml
sed -e 's/%%SDL_VERSION%%/%{version}/' %{SOURCE2002} > manifest.json
zip %{buildroot}%{TZ_SYS_APP_PREINSTALL}/%{name}.wgt config.xml manifest.json
rm config.xml manifest.json
popd
pushd src/components/HMI
zip -r %{buildroot}%{TZ_SYS_APP_PREINSTALL}/%{name}.wgt .
popd

# Create the 'hmi_link' file with the location of the sample HMI.
# Normally this would be the path to the top-level index.html file for
# the SDL HMI, e.g.  %%{_datadir}/%%{name}/HMI/index.html.  However,
# since we are using xwalk-laucher to launch the HMI we must instead
# provide the SDL Crosswalk application ID, i.s. SmartDeviceLink,
# instead.
echo %{SDL_APPID} > %{buildroot}%{_sysconfdir}/%{name}/hmi_link

%clean

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%manifest %{name}.manifest
%license LICENSE
%{_bindir}/smartDeviceLinkCore
%{_libdir}/*.so*
%config %{_sysconfdir}/%{name}/log4cxx.properties

%files sample-hmi
%manifest %{name}.manifest
%config %{_sysconfdir}/%{name}/*
%exclude %{_sysconfdir}/%{name}/log4cxx.properties
%{_datadir}/%{name}/*
%{TZ_SYS_APP_PREINSTALL}/%{name}.wgt
