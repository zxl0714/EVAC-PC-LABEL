"""Read Vulkan device timing capabilities without creating a window or device."""
import ctypes as c
import json


class ApplicationInfo(c.Structure):
    _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p),
                ('pApplicationName', c.c_char_p), ('applicationVersion', c.c_uint32),
                ('pEngineName', c.c_char_p), ('engineVersion', c.c_uint32),
                ('apiVersion', c.c_uint32)]


class InstanceCreateInfo(c.Structure):
    _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p), ('flags', c.c_uint32),
                ('pApplicationInfo', c.POINTER(ApplicationInfo)),
                ('enabledLayerCount', c.c_uint32), ('ppEnabledLayerNames', c.c_void_p),
                ('enabledExtensionCount', c.c_uint32), ('ppEnabledExtensionNames', c.c_void_p)]


class Extension(c.Structure):
    _fields_ = [('extensionName', c.c_char * 256), ('specVersion', c.c_uint32)]


class PropertyPrefix(c.Structure):
    _fields_ = [('apiVersion', c.c_uint32), ('driverVersion', c.c_uint32),
                ('vendorID', c.c_uint32), ('deviceID', c.c_uint32),
                ('deviceType', c.c_uint32), ('deviceName', c.c_char * 256)]


class PresentTimingFeatures(c.Structure):
    _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p),
                ('presentTiming', c.c_uint32), ('presentAtAbsoluteTime', c.c_uint32),
                ('presentAtRelativeTime', c.c_uint32)]


class Features2(c.Structure):
    _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p),
                ('features', c.c_uint32 * 55)]


def checked(result):
    if result != 0:
        raise RuntimeError(f'Vulkan returned {result}')


def main():
    vk = c.WinDLL('vulkan-1.dll')
    vk.vkCreateInstance.argtypes = [c.POINTER(InstanceCreateInfo), c.c_void_p,
                                    c.POINTER(c.c_void_p)]
    vk.vkEnumeratePhysicalDevices.argtypes = [c.c_void_p, c.POINTER(c.c_uint32),
                                             c.POINTER(c.c_void_p)]
    vk.vkGetPhysicalDeviceProperties.argtypes = [c.c_void_p, c.c_void_p]
    vk.vkEnumerateDeviceExtensionProperties.argtypes = [c.c_void_p, c.c_char_p,
                                                       c.POINTER(c.c_uint32), c.POINTER(Extension)]
    vk.vkDestroyInstance.argtypes = [c.c_void_p, c.c_void_p]
    vk.vkGetPhysicalDeviceFeatures2.argtypes = [c.c_void_p, c.POINTER(Features2)]
    vk.vkGetInstanceProcAddr.argtypes = [c.c_void_p, c.c_char_p]
    vk.vkGetInstanceProcAddr.restype = c.c_void_p
    app = ApplicationInfo(sType=0, pApplicationName=b'EVAC timing capability query',
                          apiVersion=(1 << 22) | (1 << 12))
    create = InstanceCreateInfo(sType=1, pApplicationInfo=c.pointer(app))
    instance = c.c_void_p()
    checked(vk.vkCreateInstance(c.byref(create), None, c.byref(instance)))
    try:
        count = c.c_uint32()
        checked(vk.vkEnumeratePhysicalDevices(instance, c.byref(count), None))
        devices = (c.c_void_p * count.value)()
        checked(vk.vkEnumeratePhysicalDevices(instance, c.byref(count), devices))
        results = []
        for device in devices[:count.value]:
            # The standard properties struct is < 1 KiB. Keep an oversized,
            # naturally aligned destination and read only its fixed prefix.
            raw = (c.c_uint64 * 512)()
            vk.vkGetPhysicalDeviceProperties(device, raw)
            props = c.cast(raw, c.POINTER(PropertyPrefix)).contents
            extension_count = c.c_uint32()
            checked(vk.vkEnumerateDeviceExtensionProperties(device, None,
                                                            c.byref(extension_count), None))
            extensions = (Extension * extension_count.value)()
            checked(vk.vkEnumerateDeviceExtensionProperties(device, None,
                                                            c.byref(extension_count), extensions))
            names = {item.extensionName.decode('ascii'): item.specVersion
                     for item in extensions[:extension_count.value]}
            features, domains = None, None
            if 'VK_EXT_present_timing' in names:
                timing = PresentTimingFeatures(sType=1000208000)
                query = Features2(sType=1000059000,
                                  pNext=c.cast(c.pointer(timing), c.c_void_p))
                vk.vkGetPhysicalDeviceFeatures2(device, c.byref(query))
                features = {name: bool(getattr(timing, name)) for name in
                            ('presentTiming', 'presentAtAbsoluteTime', 'presentAtRelativeTime')}
            if 'VK_KHR_calibrated_timestamps' in names:
                address = vk.vkGetInstanceProcAddr(instance,
                    b'vkGetPhysicalDeviceCalibrateableTimeDomainsKHR')
                if address:
                    function = c.WINFUNCTYPE(c.c_int32, c.c_void_p, c.POINTER(c.c_uint32),
                                            c.POINTER(c.c_uint32))(address)
                    domain_count = c.c_uint32()
                    checked(function(device, c.byref(domain_count), None))
                    domain_values = (c.c_uint32 * domain_count.value)()
                    checked(function(device, c.byref(domain_count), domain_values))
                    domains = list(domain_values[:domain_count.value])
            candidates = ['VK_GOOGLE_display_timing', 'VK_EXT_present_timing',
                          'VK_KHR_present_id', 'VK_KHR_present_wait',
                          'VK_KHR_present_id2', 'VK_KHR_present_wait2',
                          'VK_EXT_calibrated_timestamps', 'VK_KHR_calibrated_timestamps',
                          'VK_NV_low_latency2']
            results.append(dict(device=props.deviceName.decode('utf-8'),
                                vendor_id=props.vendorID, device_id=props.deviceID,
                                api_version=props.apiVersion, driver_version=props.driverVersion,
                                timing_extensions={name: names.get(name) for name in candidates},
                                present_timing_features=features,
                                calibratable_time_domains=domains,
                                all_extensions=names))
        print(json.dumps(dict(devices=results, scope='Extension support, not enabled features or measured latency'), indent=2))
    finally:
        vk.vkDestroyInstance(instance, None)


if __name__ == '__main__':
    main()
