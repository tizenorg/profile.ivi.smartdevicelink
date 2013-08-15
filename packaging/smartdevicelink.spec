Name:          smartdevicelink
Summary:       GENIVI SmartDeviceLink (SDL)
Version:       0.1
Release:       1
Group:         Network & Connectivity/Connection Management
License:       BSD-3-Clause
URL:           http://projects.genivi.org/smartdevicelink/
Source:        %{name}-%{version}.tar.gz
Source1001:    %{name}.manifest
BuildRequires: cmake
BuildRequires: pkgconfig(bluez)
BuildRequires: doxygen
BuildRequires: fdupes

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
# Don't run "make install".  We only care about the core SDL binary,
# and HMI related files.
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_sysconfdir}/%{name} %{buildroot}%{_datadir}/%{name}
install -m 0755 SDL_Core/src/appMain/smartDeviceLinkCore %{buildroot}%{_bindir}
install -m 0644 SDL_Core/src/appMain/log4cplus.properties %{buildroot}%{_sysconfdir}/%{name}
install -m 0644 SDL_Core/src/appMain/audio.8bit.wav %{buildroot}%{_datadir}/%{name}
cp -R SDL_Core/src/components/HMI %{buildroot}%{_datadir}/%{name}

%fdupes -s %{buildroot}%{_datadir}/%{name}

# Create the 'hmi_link' file with the location of the sample HMI.
echo %{_datadir}/%{name}/HMI/index.html > %{buildroot}%{_sysconfdir}/%{name}/hmi_link

%clean

%files
%manifest %{name}.manifest
%license LICENSE
%{_bindir}/smartDeviceLinkCore
%config %{_sysconfdir}/%{name}/log4cplus.properties
%{_datadir}/%{name}/audio.8bit.wav

%files sample-hmi
%config %{_sysconfdir}/%{name}/hmi_link
%{_datadir}/%{name}/HMI/*

#%%files doc
