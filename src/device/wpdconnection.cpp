/*
 * Akousma
 * Copyright 2026, Genki (genki_jp0) <86833542+Genki-jp0@users.noreply.github.com>
 *
 * Akousma is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Akousma is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Akousma.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

// MinGW has no PortableDeviceGUIDs library with the WPD GUIDs and property keys: define them here.
// MSVC links PortableDeviceGUIDs.lib (CMakeLists.txt).
#ifdef __MINGW32__
#  include <initguid.h>
#endif

#include <windows.h>
#include <objbase.h>
#include <propidl.h>
#include <cfgmgr32.h>
#include <portabledeviceapi.h>
#include <portabledevice.h>
#include <wrl/client.h>

#include <climits>
#include <functional>
#include <initializer_list>
#include <utility>

#include <QtGlobal>
#include <QList>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include "core/logging.h"
#include "core/song.h"
#include "constants/timeconstants.h"
#include "wpdmetadata.h"
#include "wpdconnection.h"

using namespace Qt::Literals::StringLiterals;
using Microsoft::WRL::ComPtr;

namespace {

// GUID_DEVINTERFACE_WPD (PortableDevice.h), defined here so it doesn't depend on which GUID library has it.
constexpr GUID kGuidDevinterfaceWpd = { 0x6ac27878, 0xa6fa, 0x4155, { 0xba, 0x85, 0xf9, 0x8f, 0x49, 0x1d, 0x4f, 0x33 } };

constexpr int kMaxFolderDepth = 32;
constexpr ULONG kEnumBatchSize = 64;
constexpr DWORD kDefaultBufferSize = 256 * 1024;
constexpr DWORD kMaxBufferSize = 4 * 1024 * 1024;

LPCWSTR W(const QString &s) { return reinterpret_cast<LPCWSTR>(s.utf16()); }

QString FromWide(LPCWSTR s) { return s ? QString::fromWCharArray(s) : QString(); }

QString TakeWide(LPWSTR s) {
  const QString ret = FromWide(s);
  if (s) CoTaskMemFree(s);
  return ret;
}

QString ErrorText(const HRESULT hr) {

  LPWSTR buffer = nullptr;
  const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  QString text;
  if (length > 0 && buffer) text = QString::fromWCharArray(buffer, static_cast<qsizetype>(length)).trimmed();
  if (buffer) LocalFree(buffer);
  const QString code = u"0x"_s + QString::number(static_cast<quint32>(hr), 16).rightJustified(8, u'0');
  return text.isEmpty() ? code : u"%1 (%2)"_s.arg(text, code);

}

QString GetString(IPortableDeviceValues *values, REFPROPERTYKEY key) {

  LPWSTR value = nullptr;
  if (FAILED(values->GetStringValue(key, &value))) return QString();
  return TakeWide(value);

}

quint64 GetUnsigned(IPortableDeviceValues *values, REFPROPERTYKEY key) {

  PROPVARIANT pv;
  PropVariantInit(&pv);
  quint64 ret = 0;
  if (SUCCEEDED(values->GetValue(key, &pv))) {
    switch (pv.vt) {
      case VT_UI1: ret = pv.bVal; break;
      case VT_UI2: ret = pv.uiVal; break;
      case VT_UI4: ret = pv.ulVal; break;
      case VT_UINT: ret = pv.uintVal; break;
      case VT_UI8: ret = pv.uhVal.QuadPart; break;
      case VT_I2: ret = pv.iVal > 0 ? static_cast<quint64>(pv.iVal) : 0; break;
      case VT_I4: ret = pv.lVal > 0 ? static_cast<quint64>(pv.lVal) : 0; break;
      case VT_INT: ret = pv.intVal > 0 ? static_cast<quint64>(pv.intVal) : 0; break;
      case VT_I8: ret = pv.hVal.QuadPart > 0 ? static_cast<quint64>(pv.hVal.QuadPart) : 0; break;
      default: break;
    }
  }
  PropVariantClear(&pv);
  return ret;

}

double GetDate(IPortableDeviceValues *values, REFPROPERTYKEY key) {

  PROPVARIANT pv;
  PropVariantInit(&pv);
  double ret = 0.0;
  if (SUCCEEDED(values->GetValue(key, &pv)) && pv.vt == VT_DATE) ret = pv.date;
  PropVariantClear(&pv);
  return ret;

}

QUuid GetGuid(IPortableDeviceValues *values, REFPROPERTYKEY key) {

  GUID guid;
  if (FAILED(values->GetGuidValue(key, &guid))) return QUuid();
  return QUuid(guid);

}

int ToInt(const quint64 value) { return value > static_cast<quint64>(INT_MAX) ? 0 : static_cast<int>(value); }

GUID ToGuid(const QUuid &uuid) { return static_cast<GUID>(uuid); }

ComPtr<IPortableDeviceKeyCollection> MakeKeys(std::initializer_list<PROPERTYKEY> keys) {

  ComPtr<IPortableDeviceKeyCollection> collection;
  if (FAILED(CoCreateInstance(CLSID_PortableDeviceKeyCollection, nullptr, CLSCTX_INPROC_SERVER, IID_IPortableDeviceKeyCollection, reinterpret_cast<void**>(collection.GetAddressOf())))) {
    return nullptr;
  }
  for (const PROPERTYKEY &key : keys) collection->Add(key);
  return collection;

}

ComPtr<IPortableDeviceValues> MakeValues() {

  ComPtr<IPortableDeviceValues> values;
  if (FAILED(CoCreateInstance(CLSID_PortableDeviceValues, nullptr, CLSCTX_INPROC_SERVER, IID_IPortableDeviceValues, reinterpret_cast<void**>(values.GetAddressOf())))) {
    return nullptr;
  }
  return values;

}

QStringList PropVariantStrings(IPortableDevicePropVariantCollection *collection) {

  QStringList ret;
  DWORD count = 0;
  if (!collection || FAILED(collection->GetCount(&count))) return ret;
  for (DWORD i = 0; i < count; ++i) {
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (SUCCEEDED(collection->GetAt(i, &pv)) && pv.vt == VT_LPWSTR) ret << FromWide(pv.pwszVal);
    PropVariantClear(&pv);
  }
  return ret;

}

struct NotificationRegistration {
  HCMNOTIFICATION handle;
  void (*callback)(void *context);
  void *context;
};

DWORD CALLBACK NotificationCallback(HCMNOTIFICATION handle, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA event_data, DWORD event_data_size) {

  Q_UNUSED(handle)
  Q_UNUSED(event_data)
  Q_UNUSED(event_data_size)

  if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL || action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL) {
    const NotificationRegistration *registration = static_cast<const NotificationRegistration*>(context);
    registration->callback(registration->context);
  }
  return ERROR_SUCCESS;

}

}  // namespace

WpdScopedComInit::WpdScopedComInit() : initialized_(false) {

  const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  // S_FALSE: already initialized on this thread, which still has to be balanced. RPC_E_CHANGED_MODE: the thread is in
  // a single threaded apartment (the GUI thread), which works for the free threaded WPD objects.
  initialized_ = SUCCEEDED(hr);

}

WpdScopedComInit::~WpdScopedComInit() {
  if (initialized_) CoUninitialize();
}

WpdMtaUsage::WpdMtaUsage() : cookie_(nullptr) {

  CO_MTA_USAGE_COOKIE cookie = nullptr;
  if (SUCCEEDED(CoIncrementMTAUsage(&cookie))) cookie_ = cookie;

}

WpdMtaUsage::~WpdMtaUsage() {
  if (cookie_) CoDecrementMTAUsage(static_cast<CO_MTA_USAGE_COOKIE>(cookie_));
}

struct WpdConnection::Private {
  ComPtr<IPortableDevice> device;
  ComPtr<IPortableDeviceContent> content;
  ComPtr<IPortableDeviceProperties> properties;
  ComPtr<IPortableDeviceResources> resources;

  bool Walk(const QString &parent_id, const int depth, IPortableDeviceKeyCollection *keys, const ObjectFunction &callback);
  QStringList Children(const QString &parent_id);
};

WpdConnection::WpdConnection(const QString &pnp_device_id) : d_(new Private), pnp_device_id_(pnp_device_id) {

  ComPtr<IPortableDeviceValues> client_info = MakeValues();
  if (!client_info) {
    error_text_ = u"Could not create the WPD client information"_s;
    return;
  }
  client_info->SetStringValue(WPD_CLIENT_NAME, L"Akousma");
  client_info->SetUnsignedIntegerValue(WPD_CLIENT_MAJOR_VERSION, 1);
  client_info->SetUnsignedIntegerValue(WPD_CLIENT_MINOR_VERSION, 0);
  client_info->SetUnsignedIntegerValue(WPD_CLIENT_REVISION, 0);
  client_info->SetUnsignedIntegerValue(WPD_CLIENT_SECURITY_QUALITY_OF_SERVICE, SECURITY_IMPERSONATION);
  client_info->SetUnsignedIntegerValue(WPD_CLIENT_DESIRED_ACCESS, GENERIC_READ | GENERIC_WRITE);

  // Free threaded, so the connection can be used from the loader, organize and GUI threads.
  HRESULT hr = CoCreateInstance(CLSID_PortableDeviceFTM, nullptr, CLSCTX_INPROC_SERVER, IID_IPortableDevice, reinterpret_cast<void**>(d_->device.GetAddressOf()));
  if (FAILED(hr)) {
    error_text_ = u"Could not create the WPD device object: %1"_s.arg(ErrorText(hr));
    d_->device.Reset();
    return;
  }

  hr = d_->device->Open(W(pnp_device_id), client_info.Get());
  if (hr == E_ACCESSDENIED) {
    // Some devices only allow reading.
    qLog(Warning) << "WPD device" << pnp_device_id << "refused write access, opening it read only";
    client_info->SetUnsignedIntegerValue(WPD_CLIENT_DESIRED_ACCESS, GENERIC_READ);
    hr = d_->device->Open(W(pnp_device_id), client_info.Get());
  }
  if (FAILED(hr)) {
    error_text_ = ErrorText(hr);
    d_->device.Reset();
    return;
  }

  hr = d_->device->Content(d_->content.GetAddressOf());
  if (SUCCEEDED(hr)) hr = d_->content->Properties(d_->properties.GetAddressOf());
  if (SUCCEEDED(hr)) hr = d_->content->Transfer(d_->resources.GetAddressOf());
  if (FAILED(hr)) {
    error_text_ = ErrorText(hr);
    d_->device->Close();
    d_->device.Reset();
    return;
  }

}

WpdConnection::~WpdConnection() {

  if (d_->device) d_->device->Close();

}

bool WpdConnection::is_valid() const { return d_->device && d_->content && d_->properties && d_->resources; }

QStringList WpdConnection::DevicePnpIds() {

  QStringList ret;

  ComPtr<IPortableDeviceManager> manager;
  HRESULT hr = CoCreateInstance(CLSID_PortableDeviceManager, nullptr, CLSCTX_INPROC_SERVER, IID_IPortableDeviceManager, reinterpret_cast<void**>(manager.GetAddressOf()));
  if (FAILED(hr)) {
    qLog(Error) << "Could not create the WPD device manager:" << ErrorText(hr);
    return ret;
  }

  manager->RefreshDeviceList();

  DWORD count = 0;
  hr = manager->GetDevices(nullptr, &count);
  if (FAILED(hr) || count == 0) return ret;

  QList<LPWSTR> ids(static_cast<qsizetype>(count), nullptr);
  hr = manager->GetDevices(ids.data(), &count);
  if (SUCCEEDED(hr)) {
    for (DWORD i = 0; i < count; ++i) ret << FromWide(ids[static_cast<qsizetype>(i)]);
  }
  for (LPWSTR id : std::as_const(ids)) {
    if (id) CoTaskMemFree(id);
  }

  return ret;

}

QString WpdConnection::FriendlyNameOfDevice(const QString &pnp_device_id) {

  ComPtr<IPortableDeviceManager> manager;
  if (FAILED(CoCreateInstance(CLSID_PortableDeviceManager, nullptr, CLSCTX_INPROC_SERVER, IID_IPortableDeviceManager, reinterpret_cast<void**>(manager.GetAddressOf())))) {
    return QString();
  }

  DWORD length = 0;
  if (FAILED(manager->GetDeviceFriendlyName(W(pnp_device_id), nullptr, &length)) || length == 0) return QString();
  QList<WCHAR> name(static_cast<qsizetype>(length) + 1, 0);
  if (FAILED(manager->GetDeviceFriendlyName(W(pnp_device_id), name.data(), &length))) return QString();
  return FromWide(name.constData()).trimmed();

}

void *WpdConnection::RegisterArrivalNotification(void (*callback)(void *context), void *context) {

  NotificationRegistration *registration = new NotificationRegistration { nullptr, callback, context };

  CM_NOTIFY_FILTER filter;
  ZeroMemory(&filter, sizeof(filter));
  filter.cbSize = sizeof(filter);
  filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
  filter.u.DeviceInterface.ClassGuid = kGuidDevinterfaceWpd;

  const CONFIGRET result = CM_Register_Notification(&filter, registration, NotificationCallback, &registration->handle);
  if (result != CR_SUCCESS) {
    qLog(Error) << "Could not register for WPD device notifications, error" << result;
    delete registration;
    return nullptr;
  }

  return registration;

}

void WpdConnection::UnregisterArrivalNotification(void *handle) {

  if (!handle) return;
  NotificationRegistration *registration = static_cast<NotificationRegistration*>(handle);
  // Waits for callbacks that are running.
  CM_Unregister_Notification(registration->handle);
  delete registration;

}

bool WpdConnection::ReadDeviceInfo(DeviceInfo *info) {

  if (!is_valid()) return false;

  ComPtr<IPortableDeviceKeyCollection> keys = MakeKeys({ WPD_DEVICE_FRIENDLY_NAME, WPD_DEVICE_MANUFACTURER, WPD_DEVICE_MODEL, WPD_DEVICE_SERIAL_NUMBER, WPD_DEVICE_PROTOCOL, WPD_DEVICE_TYPE });
  if (!keys) return false;

  ComPtr<IPortableDeviceValues> values;
  const HRESULT hr = d_->properties->GetValues(WPD_DEVICE_OBJECT_ID, keys.Get(), values.GetAddressOf());
  if (FAILED(hr) || !values) {
    error_text_ = ErrorText(hr);
    return false;
  }

  info->friendly_name = GetString(values.Get(), WPD_DEVICE_FRIENDLY_NAME).trimmed();
  info->manufacturer = GetString(values.Get(), WPD_DEVICE_MANUFACTURER).trimmed();
  info->model = GetString(values.Get(), WPD_DEVICE_MODEL).trimmed();
  info->serial_number = GetString(values.Get(), WPD_DEVICE_SERIAL_NUMBER).trimmed();
  info->protocol = GetString(values.Get(), WPD_DEVICE_PROTOCOL).trimmed();

  PROPVARIANT pv;
  PropVariantInit(&pv);
  info->type = -1;
  if (SUCCEEDED(values->GetValue(WPD_DEVICE_TYPE, &pv)) && (pv.vt == VT_UI4 || pv.vt == VT_I4)) info->type = static_cast<int>(pv.ulVal);
  PropVariantClear(&pv);

  return true;

}

QList<WpdConnection::StorageInfo> WpdConnection::Storages() {

  QList<StorageInfo> ret;
  if (!is_valid()) return ret;

  ComPtr<IPortableDeviceCapabilities> capabilities;
  if (FAILED(d_->device->Capabilities(capabilities.GetAddressOf()))) return ret;

  ComPtr<IPortableDevicePropVariantCollection> objects;
  if (FAILED(capabilities->GetFunctionalObjects(WPD_FUNCTIONAL_CATEGORY_STORAGE, objects.GetAddressOf()))) return ret;

  ComPtr<IPortableDeviceKeyCollection> keys = MakeKeys({ WPD_STORAGE_CAPACITY, WPD_STORAGE_FREE_SPACE_IN_BYTES, WPD_STORAGE_DESCRIPTION, WPD_OBJECT_NAME });
  if (!keys) return ret;

  const QStringList object_ids = PropVariantStrings(objects.Get());
  for (const QString &object_id : object_ids) {
    StorageInfo storage;
    storage.object_id = object_id;
    ComPtr<IPortableDeviceValues> values;
    if (SUCCEEDED(d_->properties->GetValues(W(object_id), keys.Get(), values.GetAddressOf())) && values) {
      storage.capacity = GetUnsigned(values.Get(), WPD_STORAGE_CAPACITY);
      storage.free_space = GetUnsigned(values.Get(), WPD_STORAGE_FREE_SPACE_IN_BYTES);
      storage.description = GetString(values.Get(), WPD_STORAGE_DESCRIPTION);
      if (storage.description.isEmpty()) storage.description = GetString(values.Get(), WPD_OBJECT_NAME);
    }
    ret << storage;
  }

  return ret;

}

QList<QUuid> WpdConnection::SupportedAudioFormats() {

  QList<QUuid> ret;
  if (!is_valid()) return ret;

  ComPtr<IPortableDeviceCapabilities> capabilities;
  if (FAILED(d_->device->Capabilities(capabilities.GetAddressOf()))) return ret;

  ComPtr<IPortableDevicePropVariantCollection> formats;
  if (FAILED(capabilities->GetSupportedFormats(WPD_CONTENT_TYPE_AUDIO, formats.GetAddressOf())) || !formats) return ret;

  DWORD count = 0;
  formats->GetCount(&count);
  for (DWORD i = 0; i < count; ++i) {
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (SUCCEEDED(formats->GetAt(i, &pv)) && pv.vt == VT_CLSID && pv.puuid) ret << QUuid(*pv.puuid);
    PropVariantClear(&pv);
  }

  return ret;

}

QStringList WpdConnection::Private::Children(const QString &parent_id) {

  QStringList ret;

  ComPtr<IEnumPortableDeviceObjectIDs> objects;
  const HRESULT hr = content->EnumObjects(0, W(parent_id), nullptr, objects.GetAddressOf());
  if (FAILED(hr) || !objects) {
    qLog(Warning) << "Could not list the objects in" << parent_id << ErrorText(hr);
    return ret;
  }

  while (true) {
    LPWSTR ids[kEnumBatchSize] = {};
    ULONG fetched = 0;
    const HRESULT next_hr = objects->Next(kEnumBatchSize, ids, &fetched);
    for (ULONG i = 0; i < fetched; ++i) ret << TakeWide(ids[i]);
    if (next_hr != S_OK || fetched == 0) break;
  }

  return ret;

}

bool WpdConnection::Private::Walk(const QString &parent_id, const int depth, IPortableDeviceKeyCollection *keys, const ObjectFunction &callback) {

  if (depth > kMaxFolderDepth) return true;

  const QStringList children = Children(parent_id);
  for (const QString &object_id : children) {

    ComPtr<IPortableDeviceValues> values;
    if (FAILED(properties->GetValues(W(object_id), keys, values.GetAddressOf())) || !values) continue;

    const QUuid content_type = GetGuid(values.Get(), WPD_OBJECT_CONTENT_TYPE);
    const QUuid format = GetGuid(values.Get(), WPD_OBJECT_FORMAT);
    const QString name = GetString(values.Get(), WPD_OBJECT_NAME);

    if (content_type == QUuid(WPD_CONTENT_TYPE_FOLDER) || content_type == QUuid(WPD_CONTENT_TYPE_FUNCTIONAL_OBJECT) || (content_type.isNull() && format == QUuid(WPD_OBJECT_FORMAT_PROPERTIES_ONLY))) {
      // App data and hidden folders at the top of a storage: lots of files, no music.
      if (depth == 0 && (name.compare("Android"_L1, Qt::CaseInsensitive) == 0 || name.startsWith(u'.'))) continue;
      if (!Walk(object_id, depth + 1, keys, callback)) return false;
      continue;
    }

    WpdMetadata::ObjectInfo info;
    info.object_id = object_id;
    info.name = name;
    info.original_file_name = GetString(values.Get(), WPD_OBJECT_ORIGINAL_FILE_NAME);
    info.format = format;
    info.audio_content_type = content_type == QUuid(WPD_CONTENT_TYPE_AUDIO);
    if (!WpdMetadata::IsAudioObject(info)) continue;

    info.size = GetUnsigned(values.Get(), WPD_OBJECT_SIZE);
    info.mtime = WpdMetadata::SecondsFromOleDate(GetDate(values.Get(), WPD_OBJECT_DATE_MODIFIED));
    info.title = GetString(values.Get(), WPD_MEDIA_TITLE);
    info.artist = GetString(values.Get(), WPD_MEDIA_ARTIST);
    info.album_artist = GetString(values.Get(), WPD_MEDIA_ALBUM_ARTIST);
    info.album = GetString(values.Get(), WPD_MUSIC_ALBUM);
    info.genre = GetString(values.Get(), WPD_MEDIA_GENRE);
    info.composer = GetString(values.Get(), WPD_MEDIA_COMPOSER);
    info.track = ToInt(GetUnsigned(values.Get(), WPD_MUSIC_TRACK));
    info.year = WpdMetadata::YearFromOleDate(GetDate(values.Get(), WPD_MEDIA_RELEASE_DATE));
    info.duration_ms = GetUnsigned(values.Get(), WPD_MEDIA_DURATION);
    info.samplerate = ToInt(GetUnsigned(values.Get(), WPD_MEDIA_SAMPLE_RATE));
    info.bitrate = ToInt(GetUnsigned(values.Get(), WPD_AUDIO_BITRATE));
    info.bitdepth = ToInt(GetUnsigned(values.Get(), WPD_AUDIO_BIT_DEPTH));
    info.playcount = static_cast<uint>(qMin(GetUnsigned(values.Get(), WPD_MEDIA_USE_COUNT), static_cast<quint64>(UINT_MAX)));

    if (!callback(info)) return false;

  }

  return true;

}

bool WpdConnection::ListSongs(const ObjectFunction &callback) {

  if (!is_valid()) return false;

  // Everything is read in one call per object; properties the device doesn't have are left out of the result.
  ComPtr<IPortableDeviceKeyCollection> keys = MakeKeys({ WPD_OBJECT_CONTENT_TYPE, WPD_OBJECT_FORMAT, WPD_OBJECT_NAME, WPD_OBJECT_ORIGINAL_FILE_NAME,
                                                         WPD_OBJECT_SIZE, WPD_OBJECT_DATE_MODIFIED, WPD_MEDIA_TITLE, WPD_MEDIA_ARTIST, WPD_MEDIA_ALBUM_ARTIST,
                                                         WPD_MUSIC_ALBUM, WPD_MEDIA_GENRE, WPD_MEDIA_COMPOSER, WPD_MUSIC_TRACK, WPD_MEDIA_RELEASE_DATE,
                                                         WPD_MEDIA_DURATION, WPD_MEDIA_SAMPLE_RATE, WPD_AUDIO_BITRATE, WPD_AUDIO_BIT_DEPTH, WPD_MEDIA_USE_COUNT });
  if (!keys) return false;

  const QList<StorageInfo> storages = Storages();
  for (const StorageInfo &storage : storages) {
    if (!d_->Walk(storage.object_id, 0, keys.Get(), callback)) return false;
  }

  return true;

}

bool WpdConnection::DownloadHead(const QString &object_id, const QString &filename, const qint64 max_bytes) {

  if (!is_valid()) return false;

  DWORD buffer_size = 0;
  ComPtr<IStream> stream;
  const HRESULT hr = d_->resources->GetStream(W(object_id), WPD_RESOURCE_DEFAULT, STGM_READ, &buffer_size, stream.GetAddressOf());
  if (FAILED(hr) || !stream) {
    error_text_ = ErrorText(hr);
    return false;
  }

  QFile file(filename);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

  buffer_size = qBound(static_cast<DWORD>(4096), buffer_size == 0 ? kDefaultBufferSize : buffer_size, kMaxBufferSize);
  QByteArray buffer(static_cast<qsizetype>(buffer_size), Qt::Uninitialized);
  qint64 total = 0;
  while (total < max_bytes) {
    ULONG read = 0;
    const ULONG wanted = static_cast<ULONG>(qMin(static_cast<qint64>(buffer.size()), max_bytes - total));
    const HRESULT read_hr = stream->Read(buffer.data(), wanted, &read);
    if (FAILED(read_hr) || read == 0) break;
    if (file.write(buffer.constData(), static_cast<qint64>(read)) != static_cast<qint64>(read)) return false;
    total += static_cast<qint64>(read);
    if (read_hr == S_FALSE) break;
  }

  return total > 0;

}

QString WpdConnection::FindChild(const QString &parent_id, const QString &name) {

  if (!is_valid()) return QString();

  ComPtr<IPortableDeviceKeyCollection> keys = MakeKeys({ WPD_OBJECT_NAME, WPD_OBJECT_ORIGINAL_FILE_NAME });
  if (!keys) return QString();

  const QStringList children = d_->Children(parent_id);
  for (const QString &object_id : children) {
    ComPtr<IPortableDeviceValues> values;
    if (FAILED(d_->properties->GetValues(W(object_id), keys.Get(), values.GetAddressOf())) || !values) continue;
    const QString original_file_name = GetString(values.Get(), WPD_OBJECT_ORIGINAL_FILE_NAME);
    const QString object_name = GetString(values.Get(), WPD_OBJECT_NAME);
    if (original_file_name.compare(name, Qt::CaseInsensitive) == 0 || (original_file_name.isEmpty() && object_name.compare(name, Qt::CaseInsensitive) == 0)) {
      return object_id;
    }
  }

  return QString();

}

QString WpdConnection::FindOrCreateFolder(const QString &parent_id, const QString &name) {

  const QString existing = FindChild(parent_id, name);
  if (!existing.isEmpty()) return existing;

  ComPtr<IPortableDeviceValues> values = MakeValues();
  if (!values) return QString();
  values->SetStringValue(WPD_OBJECT_PARENT_ID, W(parent_id));
  values->SetStringValue(WPD_OBJECT_NAME, W(name));
  values->SetStringValue(WPD_OBJECT_ORIGINAL_FILE_NAME, W(name));
  values->SetGuidValue(WPD_OBJECT_CONTENT_TYPE, WPD_CONTENT_TYPE_FOLDER);
  values->SetGuidValue(WPD_OBJECT_FORMAT, WPD_OBJECT_FORMAT_PROPERTIES_ONLY);

  LPWSTR object_id = nullptr;
  const HRESULT hr = d_->content->CreateObjectWithPropertiesOnly(values.Get(), &object_id);
  if (FAILED(hr)) {
    error_text_ = u"Could not create the folder %1: %2"_s.arg(name, ErrorText(hr));
    return QString();
  }

  return TakeWide(object_id);

}

QString WpdConnection::Upload(const QString &parent_id, const QString &filename, const QString &source, const Song &metadata, const ProgressFunction &progress) {

  if (!is_valid()) return QString();

  QFile file(source);
  if (!file.open(QIODevice::ReadOnly)) {
    error_text_ = u"Could not open %1: %2"_s.arg(source, file.errorString());
    return QString();
  }
  const qint64 size = file.size();

  Song::FileType filetype = metadata.filetype();
  if (filetype == Song::FileType::Unknown) filetype = Song::FiletypeByExtension(QFileInfo(filename).suffix());

  // Devices may refuse properties they don't know or formats they don't list: try with less until one is taken.
  enum class Attempt { Metadata, Format, Unspecified };
  ComPtr<IStream> stream;
  DWORD buffer_size = 0;
  HRESULT hr = E_FAIL;
  for (const Attempt attempt : { Attempt::Metadata, Attempt::Format, Attempt::Unspecified }) {
    ComPtr<IPortableDeviceValues> values = MakeValues();
    if (!values) return QString();
    values->SetStringValue(WPD_OBJECT_PARENT_ID, W(parent_id));
    values->SetStringValue(WPD_OBJECT_NAME, W(filename));
    values->SetStringValue(WPD_OBJECT_ORIGINAL_FILE_NAME, W(filename));
    values->SetUnsignedLargeIntegerValue(WPD_OBJECT_SIZE, static_cast<ULONGLONG>(size));
    values->SetGuidValue(WPD_OBJECT_CONTENT_TYPE, WPD_CONTENT_TYPE_AUDIO);
    const QUuid format = attempt == Attempt::Unspecified ? WpdMetadata::MtpFormat(0x3000) : WpdMetadata::FormatForFileType(filetype);
    values->SetGuidValue(WPD_OBJECT_FORMAT, ToGuid(format));
    if (attempt == Attempt::Metadata) {
      if (!metadata.title().isEmpty()) values->SetStringValue(WPD_MEDIA_TITLE, W(metadata.title()));
      const QString artist = metadata.artist().isEmpty() ? metadata.effective_albumartist() : metadata.artist();
      if (!artist.isEmpty()) values->SetStringValue(WPD_MEDIA_ARTIST, W(artist));
      if (!metadata.albumartist().isEmpty()) values->SetStringValue(WPD_MEDIA_ALBUM_ARTIST, W(metadata.albumartist()));
      if (!metadata.album().isEmpty()) values->SetStringValue(WPD_MUSIC_ALBUM, W(metadata.album()));
      if (!metadata.genre().isEmpty()) values->SetStringValue(WPD_MEDIA_GENRE, W(metadata.genre()));
      if (!metadata.composer().isEmpty()) values->SetStringValue(WPD_MEDIA_COMPOSER, W(metadata.composer()));
      if (metadata.track() > 0) values->SetUnsignedIntegerValue(WPD_MUSIC_TRACK, static_cast<ULONG>(metadata.track()));
      if (metadata.length_nanosec() > 0) values->SetUnsignedLargeIntegerValue(WPD_MEDIA_DURATION, static_cast<ULONGLONG>(metadata.length_nanosec() / kNsecPerMsec));
    }

    stream.Reset();
    hr = d_->content->CreateObjectWithPropertiesAndData(values.Get(), stream.GetAddressOf(), &buffer_size, nullptr);
    if (SUCCEEDED(hr) && stream) break;
    qLog(Debug) << "WPD device refused" << filename << "with" << (attempt == Attempt::Metadata ? "metadata" : attempt == Attempt::Format ? "its format" : "an unspecified format") << ErrorText(hr);
  }
  if (FAILED(hr) || !stream) {
    error_text_ = u"Could not create %1 on the device: %2"_s.arg(filename, ErrorText(hr));
    return QString();
  }

  buffer_size = qBound(static_cast<DWORD>(4096), buffer_size == 0 ? kDefaultBufferSize : buffer_size, kMaxBufferSize);
  QByteArray buffer(static_cast<qsizetype>(buffer_size), Qt::Uninitialized);
  qint64 sent = 0;
  while (sent < size) {
    const qint64 read = file.read(buffer.data(), buffer.size());
    if (read <= 0) {
      error_text_ = u"Could not read %1: %2"_s.arg(source, file.errorString());
      return QString();  // Not committed: the device drops the object.
    }
    ULONG written = 0;
    hr = stream->Write(buffer.constData(), static_cast<ULONG>(read), &written);
    if (FAILED(hr) || written != static_cast<ULONG>(read)) {
      error_text_ = u"Could not write %1 to the device: %2"_s.arg(filename, ErrorText(FAILED(hr) ? hr : STG_E_MEDIUMFULL));
      return QString();
    }
    sent += read;
    if (progress) progress(size > 0 ? static_cast<float>(sent) / static_cast<float>(size) : 1.0F);
  }

  hr = stream->Commit(STGC_DEFAULT);
  if (FAILED(hr)) {
    error_text_ = u"Could not finish %1 on the device: %2"_s.arg(filename, ErrorText(hr));
    return QString();
  }

  ComPtr<IPortableDeviceDataStream> data_stream;
  LPWSTR object_id = nullptr;
  if (FAILED(stream->QueryInterface(IID_IPortableDeviceDataStream, reinterpret_cast<void**>(data_stream.GetAddressOf()))) || FAILED(data_stream->GetObjectID(&object_id))) {
    error_text_ = u"The device didn't tell the ID of %1"_s.arg(filename);
    return QString();
  }

  return TakeWide(object_id);

}

bool WpdConnection::Delete(const QString &object_id) {

  if (!is_valid()) return false;

  ComPtr<IPortableDevicePropVariantCollection> objects;
  HRESULT hr = CoCreateInstance(CLSID_PortableDevicePropVariantCollection, nullptr, CLSCTX_INPROC_SERVER, IID_IPortableDevicePropVariantCollection, reinterpret_cast<void**>(objects.GetAddressOf()));
  if (FAILED(hr)) return false;

  PROPVARIANT pv;
  PropVariantInit(&pv);
  pv.vt = VT_LPWSTR;
  pv.pwszVal = const_cast<LPWSTR>(W(object_id));
  hr = objects->Add(&pv);  // Copies the string.
  if (FAILED(hr)) return false;

  hr = d_->content->Delete(PORTABLE_DEVICE_DELETE_NO_RECURSION, objects.Get(), nullptr);
  if (FAILED(hr)) {
    error_text_ = ErrorText(hr);
    qLog(Error) << "Could not delete" << object_id << "from the WPD device:" << error_text_;
    return false;
  }

  return true;

}
