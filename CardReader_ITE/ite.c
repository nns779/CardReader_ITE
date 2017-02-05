// ite.c

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>
#include <ks.h>

#include "debug.h"
#include "ite.h"

#ifdef _ITE_MUTEX
static const wchar_t mutex_name[] = L"ite_mutex {1B5EA8EA-4A3E-493F-A030-B52196935F99}";
#endif

static const GUID KSPROPSETID_ITEDeviceControl = { 0xf23fac2d, 0xe1af, 0x48e0,{ 0x8b, 0xbe, 0xa1, 0x40, 0x29, 0xc9, 0x2f, 0x11 } };
static const GUID KSPROPSETID_ITESatControl = { 0xf23fac2d, 0xe1af, 0x48e0,{ 0x8b, 0xbe, 0xa1, 0x40, 0x29, 0xc9, 0x2f, 0x21 } };

bool ite_close(ite_dev *const dev);

bool ite_init(ite_dev *const dev)
{
	dev->dev = INVALID_HANDLE_VALUE;

#ifdef _ITE_MUTEX
	dev->mutex = NULL;

	HANDLE mutex;

	mutex = CreateMutexW(NULL, FALSE, mutex_name);
	if (mutex == NULL) {
		win32_err(L"ite_init: CreateMutexW");
		return false;
	}

	dev->mutex = mutex;
#endif

	return true;
}

bool ite_release(ite_dev *const dev)
{
	ite_close(dev);

#ifdef _ITE_MUTEX
	CloseHandle(dev->mutex);
	dev->mutex = NULL;
#endif

	return true;
}

bool ite_open(ite_dev *const dev, const wchar_t *const path)
{
	HANDLE device;

	if (dev->dev != INVALID_HANDLE_VALUE) {
		dbg_wprintf(L"ite_open: already be opened");
		return false;
	}

	device = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (device == INVALID_HANDLE_VALUE) {
		win32_err(L"ite_open: CreateFileW");
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

bool ite_lock(ite_dev *const dev)
{
#ifdef _ITE_MUTEX
	WaitForSingleObject(dev->mutex, INFINITE);
#endif
	return true;
}

bool ite_unlock(ite_dev *const dev)
{
#ifdef _ITE_MUTEX
	ReleaseMutex(dev->mutex);
#endif
	return true;
}

bool ite_dev_ioctl_nolock(ite_dev *const dev, const uint32_t code, const ite_ioctl_type type, const void *const in, const uint32_t in_size, const void *const out, const uint32_t out_size)
{
	KSPROPERTY prop;
	ULONG rb;

	prop.Set = KSPROPSETID_ITEDeviceControl;
	prop.Id = code;
	prop.Flags = KSPROPERTY_TYPE_SET;

	if (DeviceIoControl(dev->dev, IOCTL_KS_PROPERTY, (void *)&prop, sizeof(prop), (void *)in, in_size, &rb, NULL) == FALSE) {
		win32_err(L"ite_dev_ioctl_nolock: DeviceIoControl (Property SET)");
		return false;
	}
	else if (in_size != rb) {
		internal_err(L"ite_dev_ioctl_nolock: data lost (Property SET)");
		return false;
	}

	if (type == ITE_IOCTL_IN)
	{
		prop.Flags = KSPROPERTY_TYPE_GET;

		if (DeviceIoControl(dev->dev, IOCTL_KS_PROPERTY, (void *)&prop, sizeof(prop), (void *)out, out_size, &rb, NULL) == FALSE) {
			win32_err(L"ite_dev_ioctl_nolock: DeviceIoControl (Property GET)");
			return false;
		}
		else if (out_size != rb) {
			internal_err(L"ite_dev_ioctl_nolock: data lost (Property GET)");
			return false;
		}
	}

	return true;
}

bool ite_dev_ioctl(ite_dev *const dev, const uint32_t code, const ite_ioctl_type type, const void *const in, const uint32_t in_size, const void *const out, const uint32_t out_size)
{
	if (dev == NULL)
		return false;

	bool r;

	ite_lock(dev);
	r = ite_dev_ioctl_nolock(dev, code, type, in, in_size, out, out_size);
	ite_unlock(dev);

	return r;
}

bool ite_devctl_nolock(ite_dev *const dev, const ite_ioctl_type type, struct ite_devctl_data *const data)
{
	if (dev == NULL || data == NULL)
		return false;

	return ite_dev_ioctl_nolock(dev, 1, type, data, sizeof(struct ite_devctl_data), data, sizeof(struct ite_devctl_data));
}

bool ite_devctl(ite_dev *const dev, const ite_ioctl_type type, struct ite_devctl_data *const data)
{
	if (dev == NULL || data == NULL)
		return false;

	bool r;

	ite_lock(dev);
	r = ite_dev_ioctl_nolock(dev, 1, type, data, sizeof(struct ite_devctl_data), data, sizeof(struct ite_devctl_data));
	ite_unlock(dev);

	return r;
}

bool ite_sat_ioctl_nolock(ite_dev *const dev, const uint32_t code, const ite_ioctl_type type, const void *const data, const uint32_t data_size)
{
	KSPROPERTY prop;
	ULONG rb;

	prop.Set = KSPROPSETID_ITESatControl;
	prop.Id = code;

	if (type == ITE_IOCTL_IN)
	{
		prop.Flags = KSPROPERTY_TYPE_GET;

		if (DeviceIoControl(dev->dev, IOCTL_KS_PROPERTY, (void *)&prop, sizeof(prop), (void *)data, data_size, &rb, NULL) == FALSE) {
			win32_err(L"ite_sat_ioctl_nolock: DeviceIoControl (Property GET)");
			return false;
		}

		return true;
	}
	else if (type == ITE_IOCTL_OUT)
	{
		prop.Flags = KSPROPERTY_TYPE_SET;

		if (DeviceIoControl(dev->dev, IOCTL_KS_PROPERTY, (void *)&prop, sizeof(prop), (void*)data, data_size, &rb, NULL) == FALSE) {
			win32_err(L"ite_sat_ioctl_nolock: DeviceIoControl (Property SET)");
			return false;
		}

		return true;
	}

	return false;
}

bool ite_sat_ioctl(ite_dev *const dev, const uint32_t code, const ite_ioctl_type type, const void *const data, const uint32_t data_size)
{
	if (dev == NULL || data == NULL || data_size == 0)
		return false;

	bool r;

	ite_lock(dev);
	r = ite_sat_ioctl_nolock(dev, code, type, data, data_size);
	ite_unlock(dev);

	return r;
}
