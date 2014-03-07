Name:          smartdevicelink
Summary:       GENIVI SmartDeviceLink (SDL)
Version:       2.0
Release:       1
Group:         Network & Connectivity/Connection Management
License:       BSD-3-Clause
URL:           http://projects.genivi.org/smartdevicelink/
Source:        %{name}-%{version}.tar.gz
Source1:       %{name}.xml
Source1001:    %{name}.manifest
BuildRequires: cmake
BuildRequires: pkgconfig(bluez)
BuildRequires: pkgconfig(gstreamer-1.0)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(liblog4cxx)
BuildRequires: pkgconfig(avahi-client)
BuildRequires: pkgconfig(libpulse-simple)
BuildRequires: doxygen
BuildRequires: fdupes
Requires:      avahi-libs
Requires(post): /usr/bin/pkg_initdb

# For MiniBrowser
Requires:      webkit2-efl-test

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
Group:         Graphics & UI Framework/Automotive UI
Summary:       Sample SmartDeviceLink capable HMI
Provides:      smartdevicelink-hmi

%description sample-hmi
This package contains a an SmartDeviceLink template-based
sample/reference HMI.

%prep
%setup -q -n %{name}-%{version}
cp %{SOURCE1001} .

%build
cd SDL_Core
%cmake .
make %{?_smp_mflags}

%install
# Don't run "make install".  We only care about the core SDL related
# binaries, and HMI related files.
mkdir -p %{buildroot}%{_sysconfdir}/%{name}
install -m 0644 SDL_Core/src/appMain/log4cxx.properties %{buildroot}%{_sysconfdir}/%{name}

mkdir -p %{buildroot}%{_bindir} %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/appMain/smartDeviceLinkCore %{buildroot}%{_bindir}
install -m 0755 SDL_Core/src/components/audio_manager/libAudioManager.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/components/utils/libUtils.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/components/connection_handler/libconnectionHandler.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/components/request_watchdog/libRequestWatchdog.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/components/mobile_message_handler/libMobileMessageHandler.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/components/smart_objects/libSmartObjects.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/components/transport_manager/libTransportManager.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/components/protocol_handler/libProtocolHandler.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/components/hmi_message_handler/libHMIMessageHandler.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/components/config_profile/libConfigProfile.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/components/interfaces/libMOBILE_API.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/components/interfaces/libHMI_API.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/components/application_manager/libApplicationManager.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/components/formatters/libformatters.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/thirdPartyLibs/encryption/libencryption.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/thirdPartyLibs/MessageBroker/libMessageBrokerClient.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/thirdPartyLibs/MessageBroker/libMessageBrokerServer.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/thirdPartyLibs/MessageBroker/libMessageBroker.so %{buildroot}%{_libdir}
install -m 0755 SDL_Core/src/thirdPartyLibs/jsoncpp/libjsoncpp.so %{buildroot}%{_libdir}

# Sample HMI
mkdir -p %{buildroot}%{_datadir}/%{name}
install -m 0644 SDL_Core/src/appMain/audio.8bit.wav %{buildroot}%{_datadir}/%{name}
cp -R SDL_Core/src/components/HMI %{buildroot}%{_datadir}/%{name}
%fdupes -s %{buildroot}%{_datadir}/%{name}

# Create the 'hmi_link' file with the location of the sample HMI.
echo %{_datadir}/%{name}/HMI/index.html > %{buildroot}%{_sysconfdir}/%{name}/hmi_link

# Install Tizen package metadata for smartdevicelink
mkdir -p %{buildroot}%{_datadir}/packages/
mkdir -p %{buildroot}%{_datadir}/icons/default/small
install -m 0644 %{SOURCE1} %{buildroot}%{_datadir}/packages/%{name}.xml
ln -sf %{_datadir}/%{name}/HMI/images/sdl/devices.png %{buildroot}%{_datadir}/icons/default/small/

%clean

%post -p /sbin/ldconfig

%post sample-hmi
/usr/bin/pkg_initdb

%postun -p /sbin/ldconfig

%files
%manifest %{name}.manifest
%license LICENSE
%{_bindir}/smartDeviceLinkCore
%{_libdir}/*.so*
%config %{_sysconfdir}/%{name}/log4cxx.properties
%{_datadir}/%{name}/audio.8bit.wav

%files sample-hmi
%config %{_sysconfdir}/%{name}/hmi_link
%{_datadir}/%{name}/HMI/*
%{_datadir}/packages/%{name}.xml
%{_datadir}/icons/default/small/*.png
