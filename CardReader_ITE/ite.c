// ite.c

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>
#include <ks.h>

#include "debug.h"
#include "ite.h"

static const GUID KSPROPSETID_IteDevice = { 0xc6efe5eb, 0x855a, 0x4f1b, { 0xb7, 0xaa, 0x87, 0xb5, 0xe1, 0xdc, 0x41, 0x13} };
static const GUID KSPROPSETID_IteDeviceControl = { 0xf23fac2d, 0xe1af, 0x48e0, { 0x8b, 0xbe, 0xa1, 0x40, 0x29, 0xc9, 0x2f, 0x11 } };
static const GUID KSPROPSETID_IteSatControl = { 0xf23fac2d, 0xe1af, 0x48e0, { 0x8b, 0xbe, 0xa1, 0x40, 0x29, 0xc9, 0x2f, 0x21 } };

bool ite_open(ite_dev *const dev, const wchar_t *const path)
{
	HANDLE device;

	if (ite_close(dev) == false) {
		internal_err("ite_open: ite_close failed");
		return false;
	}

	device = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
	if (device == INVALID_HANDLE_VALUE) {
		win32_err("ite_open: CreateFileW");
		return false;
	}

	dev->dev = device;

	return true;
}

bool ite_close(ite_dev *const dev)
{
	if (dev->dev != INVALID_HANDLE_VALUE) {
		CloseHandle(dev->dev);
		dev->dev = INVALID_HANDLE_VALUE;
	}

	return true;
}

static bool _init_overlapped(OVERLAPPED *const overlapped)
{
	memset(overlapped, 0, sizeof(OVERLAPPED));

	overlapped->hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (overlapped->hEvent == NULL) {
		win32_err("_init_overlapped: CreateEventW");
		return false;
	}

	return true;
}

static void _release_overlapped(OVERLAPPED *const overlapped)
{
	CloseHandle(overlapped->hEvent);
}

static bool _dev_io_control(ite_dev *const dev, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped)
{
	BOOL ret;

	ret = DeviceIoControl(dev->dev, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesReturned, lpOverlapped);
	if (ret == FALSE)
	{
		if (GetLastError() == ERROR_IO_PENDING) {
			ret = GetOverlappedResult(dev->dev, lpOverlapped, lpBytesReturned, TRUE);
			if (ret == FALSE) {
				win32_err("_dev_io_control: GetOverlappedResult");
			}
		}
		else {
			win32_err("_dev_io_control: DeviceIoControl");
		}
	}

	ResetEvent(lpOverlapped->hEvent);

	return (ret == FALSE) ? false : true;
}

bool ite_dev_ioctl(ite_dev *const dev, const uint32_t code, const ite_ioctl_type type, const void *const in, const uint32_t in_size, const void *const out, const uint32_t out_size)
{
	KSPROPERTY prop;
	ULONG rb = 0;
	OVERLAPPED overlapped;

	prop.Set = KSPROPSETID_IteDeviceControl;
	prop.Id = code;
	prop.Flags = KSPROPERTY_TYPE_SET;

	if (_init_overlapped(&overlapped) == false) {
		internal_err("ite_dev_ioctl: _init_overlapped failed");
		return false;
	}

	bool r = true;

	if (_dev_io_control(dev, IOCTL_KS_PROPERTY, (void *)&prop, sizeof(prop), (void *)in, in_size, &rb, &overlapped) == false) {
		internal_err("ite_dev_ioctl: _dev_io_control failed (Property SET)");
		r = false;
		goto end;
	}
	else if (in_size != rb) {
		internal_err("ite_dev_ioctl: data lost (Property SET)");
		r = false;
		goto end;
	}

	if (type == ITE_IOCTL_IN)
	{
		prop.Flags = KSPROPERTY_TYPE_GET;

		rb = 0;

		if (_dev_io_control(dev, IOCTL_KS_PROPERTY, (void *)&prop, sizeof(prop), (void *)out, out_size, &rb, &overlapped) == false) {
			internal_err("ite_dev_ioctl: _dev_io_control failed (Property GET)");
			r = false;
			goto end;
		}
		else if (out_size != rb) {
			internal_err("ite_dev_ioctl: data lost (Property GET)");
			r = false;
			goto end;
		}
	}

end:
	_release_overlapped(&overlapped);

	return r;
}

bool ite_devctl(ite_dev *const dev, const ite_ioctl_type type, struct ite_devctl_data *const data)
{
	if (dev == NULL || data == NULL)
		return false;

	return ite_dev_ioctl(dev, 1, type, data, sizeof(struct ite_devctl_data), data, sizeof(struct ite_devctl_data));
}

bool ite_sat_ioctl(ite_dev *const dev, const uint32_t code, const ite_ioctl_type type, const void *const data, const uint32_t data_size)
{
	KSPROPERTY prop;
	ULONG rb = 0;
	OVERLAPPED overlapped;

	prop.Set = KSPROPSETID_IteSatControl;
	prop.Id = code;

	if (_init_overlapped(&overlapped) == false) {
		internal_err("ite_sat_ioctl: _init_overlapped failed");
		return false;
	}

	bool r = true;

	if (type == ITE_IOCTL_IN)
	{
		prop.Flags = KSPROPERTY_TYPE_GET;

		if (_dev_io_control(dev, IOCTL_KS_PROPERTY, (void *)&prop, sizeof(prop), (void *)data, data_size, &rb, &overlapped) == false) {
			internal_err("ite_sat_ioctl: _dev_io_control failed (Property GET)");
			r = false;
		}
	}
	else if (type == ITE_IOCTL_OUT)
	{
		prop.Flags = KSPROPERTY_TYPE_SET;

		if (_dev_io_control(dev, IOCTL_KS_PROPERTY, (void *)&prop, sizeof(prop), (void*)data, data_size, &rb, &overlapped) == false) {
			internal_err("ite_sat_ioctl: _dev_io_control failed (Property SET)");
			r = false;
		}
	}

	_release_overlapped(&overlapped);

	return r;
}
