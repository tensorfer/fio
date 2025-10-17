Name:           fio-gd2fs
Version:        20260616
Release:        1%{?dist}
Summary:        FIO with libgd2fs supported

License:        Copyright (c) 2025 Beijing Tensorfer Co., Ltd. All rights reserved.
URL:            https://tensorfer.com
Packager:       Tensorfer <support@tensorfer.com>

BuildArch:      x86_64
AutoReqProv:    no

%description
FIO with libgd2fs supported

%install
mkdir -p %{buildroot}/usr/bin

install ${XFER_FIO}/fio %{buildroot}/usr/bin/fio-gd2fs

%files
/usr/bin/fio-gd2fs

%changelog
