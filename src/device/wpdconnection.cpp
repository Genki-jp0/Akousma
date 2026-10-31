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

#include <atomic>
#include <climits>
#include <functional>
#include <initializer_list>
#include <utility>

#include <QtGlobal>
#include <QList>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QElapsedTimer>
#include <QScopeGuard>
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

constexpr char kLogCategory[] = "WpdConnection";

constexpr int kMaxFolderDepth = 32;
// Bulk reads: objects per request, how long to wait for a request that shows no progress, and for a cancelled one.
constexpr qsizetype kBulkChunk = 256;
constexpr DWORD kBulkWaitStepMsec = 100;
constexpr qint64 kBulkIdleTimeoutMsec = 30000;
constexpr DWORD kBulkCancelWaitMsec = 5000;
constexpr int kMaxBulkFailures = 3;
// Folders that take longer than this are logged.
constexpr qint64 kSlowFolderMsec = 5000;
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

// Bulk property reads (IPortableDevicePropertiesBulk) report to this callback, on a WPD thread.
class BulkCallback final : public IPortableDevicePropertiesBulkCallback {
 public:
  BulkCallback() : refs_(1), progress_ticks_(0), status_(E_PENDING), done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

  HANDLE done() const { return done_; }
  LONG progress_ticks() { return InterlockedCompareExchange(&progress_ticks_, 0, 0); }
  HRESULT status() { return static_cast<HRESULT>(InterlockedCompareExchange(&status_, 0, 0)); }

  QList<ComPtr<IPortableDeviceValues>> TakeResults() {
    QMutexLocker l(&mutex_);
    QList<ComPtr<IPortableDeviceValues>> ret;
    ret.swap(results_);
    return ret;
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
    if (!object) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IPortableDevicePropertiesBulkCallback)) {
      *object = static_cast<IPortableDevicePropertiesBulkCallback*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }

  ULONG STDMETHODCALLTYPE Release() override {
    const LONG refs = InterlockedDecrement(&refs_);
    if (refs == 0) delete this;
    return static_cast<ULONG>(refs);
  }

  HRESULT STDMETHODCALLTYPE OnStart(REFGUID context) override {
    Q_UNUSED(context)
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnProgress(REFGUID context, IPortableDeviceValuesCollection *results) override {

    Q_UNUSED(context)

    if (results) {
      DWORD count = 0;
      if (SUCCEEDED(results->GetCount(&count))) {
        QMutexLocker l(&mutex_);
        for (DWORD i = 0; i < count; ++i) {
          ComPtr<IPortableDeviceValues> values;
          if (SUCCEEDED(results->GetAt(i, values.GetAddressOf())) && values) results_ << values;
        }
      }
    }
    InterlockedIncrement(&progress_ticks_);
    return S_OK;

  }

  HRESULT STDMETHODCALLTYPE OnEnd(REFGUID context, HRESULT hr_status) override {
    Q_UNUSED(context)
    InterlockedExchange(&status_, static_cast<LONG>(hr_status));
    if (done_) SetEvent(done_);
    return S_OK;
  }

 private:
  ~BulkCallback() {
    if (done_) CloseHandle(done_);
  }

  Q_DISABLE_COPY(BulkCallback)

  LONG volatile refs_;
  LONG volatile progress_ticks_;
  LONG volatile status_;
  HANDLE done_;
  QMutex mutex_;
  QList<ComPtr<IPortableDeviceValues>> results_;
};

WpdConnection::ErrorKind ErrorKindOf(const HRESULT hr) {

  if (hr == E_ACCESSDENIED) return WpdConnection::ErrorKind::AccessDenied;
  if (hr == HRESULT_FROM_WIN32(ERROR_BUSY) || hr == HRESULT_FROM_WIN32(ERROR_NOT_READY) || hr == HRESULT_FROM_WIN32(ERROR_SEM_TIMEOUT) || hr == HRESULT_FROM_WIN32(ERROR_TIMEOUT)) {
    return WpdConnection::ErrorKind::Busy;
  }
  return WpdConnection::ErrorKind::Other;

}

bool IsFolder(const QUuid &content_type, const QUuid &format) {
  return content_type == QUuid(WPD_CONTENT_TYPE_FOLDER) || content_type == QUuid(WPD_CONTENT_TYPE_FUNCTIONAL_OBJECT) || (content_type.isNull() && format == QUuid(WPD_OBJECT_FORMAT_PROPERTIES_ONLY));
}

ComPtr<IPortableDevicePropVariantCollection> MakeObjectIds(const QStringList &object_ids) {

  ComPtr<IPortableDevicePropVariantCollection> collection;
  if (FAILED(CoCreateInstance(CLSID_PortableDevicePropVariantCollection, nullptr, CLSCTX_INPROC_SERVER, IID_IPortableDevicePropVariantCollection, reinterpret_cast<void**>(collection.GetAddressOf())))) {
    return nullptr;
  }
  for (const QString &object_id : object_ids) {
    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt = VT_LPWSTR;
    pv.pwszVal = const_cast<LPWSTR>(W(object_id));
    if (FAILED(collection->Add(&pv))) return nullptr;  // Add() copies the string.
  }
  return collection;

}

void FillSongInfo(IPortableDeviceValues *values, WpdMetadata::ObjectInfo *info) {

  info->size = GetUnsigned(values, WPD_OBJECT_SIZE);
  info->mtime = WpdMetadata::SecondsFromOleDate(GetDate(values, WPD_OBJECT_DATE_MODIFIED));
  info->title = GetString(values, WPD_MEDIA_TITLE);
  info->artist = GetString(values, WPD_MEDIA_ARTIST);
  info->album_artist = GetString(values, WPD_MEDIA_ALBUM_ARTIST);
  info->album = GetString(values, WPD_MUSIC_ALBUM);
  info->genre = GetString(values, WPD_MEDIA_GENRE);
  info->composer = GetString(values, WPD_MEDIA_COMPOSER);
  info->track = ToInt(GetUnsigned(values, WPD_MUSIC_TRACK));
  info->year = WpdMetadata::YearFromOleDate(GetDate(values, WPD_MEDIA_RELEASE_DATE));
  info->duration_ms = GetUnsigned(values, WPD_MEDIA_DURATION);
  info->samplerate = ToInt(GetUnsigned(values, WPD_MEDIA_SAMPLE_RATE));
  info->bitrate = ToInt(GetUnsigned(values, WPD_AUDIO_BITRATE));
  info->bitdepth = ToInt(GetUnsigned(values, WPD_AUDIO_BIT_DEPTH));
  info->playcount = static_cast<uint>(qMin(GetUnsigned(values, WPD_MEDIA_USE_COUNT), static_cast<quint64>(UINT_MAX)));

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

using ValuesMap = QHash<QString, ComPtr<IPortableDeviceValues>>;

struct WpdConnection::Private {
  ComPtr<IPortableDevice> device;
  ComPtr<IPortableDeviceContent> content;
  ComPtr<IPortableDeviceProperties> properties;
  ComPtr<IPortableDeviceResources> resources;
  ComPtr<IPortableDevicePropertiesBulk> bulk;
  bool bulk_usable = true;
  int bulk_failures = 0;

  std::atomic<bool> cancelled { false };

  // Guards the interfaces against Cancel() from another thread while Open() sets them, and the current operation.
  mutable QMutex mutex;
  QString current_operation;
  QElapsedTimer current_operation_timer;
  // The enumerator and stream in use, which have Cancel()s of their own.
  ComPtr<IEnumPortableDeviceObjectIDs> current_enum;
  ComPtr<IPortableDeviceDataStream> current_stream;

  // The WPD call being made, for CurrentOperation() (a watchdog in another thread logs calls that take long).
  class Operation {
   public:
    Operation(Private *d, const QString &operation) : d_(d) {
      QMutexLocker l(&d_->mutex);
      d_->current_operation = operation;
      d_->current_operation_timer.start();
    }
    ~Operation() {
      QMutexLocker l(&d_->mutex);
      d_->current_operation.clear();
    }

   private:
    Q_DISABLE_COPY(Operation)
    Private *d_;
  };

  struct WalkState {
    ComPtr<IPortableDeviceKeyCollection> object_keys;
    ComPtr<IPortableDeviceKeyCollection> song_keys;
    ObjectFunction callback;
    WalkProgressFunction progress_callback;
    WalkProgress progress;
    int unreadable = 0;
  };

  enum class BulkResult { Ok, Unsupported, Failed, Cancelled };

  bool Walk(const QString &parent_id, const QString &path, const int depth, const WpdMetadata::ProgressSpan &span, WalkState *state);
  bool Children(const QString &parent_id, const QString &path, QStringList *ret);
  QStringList Children(const QString &parent_id);
  bool ReadValues(const QStringList &object_ids, IPortableDeviceKeyCollection *keys, const QString &path, ValuesMap *ret);
  BulkResult ReadBulk(const QStringList &object_ids, IPortableDeviceKeyCollection *keys, const QString &path, ValuesMap *ret);
  bool ReadEach(const QStringList &object_ids, IPortableDeviceKeyCollection *keys, const QString &path, ValuesMap *ret);
};

WpdConnection::WpdConnection(const QString &pnp_device_id, const bool open) : d_(new Private), pnp_device_id_(pnp_device_id), error_kind_(ErrorKind::None) {

  // Free threaded, so the connection can be used from the loader, organize and GUI threads.
  const HRESULT hr = CoCreateInstance(CLSID_PortableDeviceFTM, nullptr, CLSCTX_INPROC_SERVER, IID_IPortableDevice, reinterpret_cast<void**>(d_->device.GetAddressOf()));
  if (FAILED(hr)) {
    error_text_ = u"Could not create the WPD device object: %1"_s.arg(ErrorText(hr));
    error_kind_ = ErrorKind::Other;
    d_->device.Reset();
    return;
  }

  if (open) Open();

}

WpdConnection::~WpdConnection() {

  ComPtr<IPortableDevice> device;
  {
    QMutexLocker l(&d_->mutex);
    if (d_->content) device = d_->device;
  }
  if (device) device->Close();

}

bool WpdConnection::Open() {

  if (!d_->device) return false;
  if (is_valid()) return true;
  if (cancelled()) {
    error_text_ = u"Cancelled"_s;
    error_kind_ = ErrorKind::Other;
    return false;
  }

  ComPtr<IPortableDeviceValues> client_info = MakeValues();
  if (!client_info) {
    error_text_ = u"Could not create the WPD client information"_s;
    error_kind_ = ErrorKind::Other;
    return false;
  }
  client_info->SetStringValue(WPD_CLIENT_NAME, L"Akousma");
  client_info->SetUnsignedIntegerValue(WPD_CLIENT_MAJOR_VERSION, 1);
  client_info->SetUnsignedIntegerValue(WPD_CLIENT_MINOR_VERSION, 0);
  client_info->SetUnsignedIntegerValue(WPD_CLIENT_REVISION, 0);
  client_info->SetUnsignedIntegerValue(WPD_CLIENT_SECURITY_QUALITY_OF_SERVICE, SECURITY_IMPERSONATION);
  client_info->SetUnsignedIntegerValue(WPD_CLIENT_DESIRED_ACCESS, GENERIC_READ | GENERIC_WRITE);

  QElapsedTimer timer;
  timer.start();

  HRESULT hr = E_FAIL;
  {
    Private::Operation operation(d_.get(), u"IPortableDevice::Open"_s);
    hr = d_->device->Open(W(pnp_device_id_), client_info.Get());
  }
  if (hr == E_ACCESSDENIED && !cancelled()) {
    // Some devices only allow reading.
    qLog(Warning) << "WPD device" << pnp_device_id_ << "refused write access, opening it read only";
    client_info->SetUnsignedIntegerValue(WPD_CLIENT_DESIRED_ACCESS, GENERIC_READ);
    Private::Operation operation(d_.get(), u"IPortableDevice::Open (read only)"_s);
    hr = d_->device->Open(W(pnp_device_id_), client_info.Get());
  }
  if (FAILED(hr)) {
    error_text_ = ErrorText(hr);
    error_kind_ = ErrorKindOf(hr);
    if (timer.elapsed() > 1000) qLog(Warning) << "Opening WPD device" << pnp_device_id_ << "failed after" << timer.elapsed() << "ms:" << error_text_;
    return false;
  }

  ComPtr<IPortableDeviceContent> content;
  ComPtr<IPortableDeviceProperties> properties;
  ComPtr<IPortableDeviceResources> resources;
  hr = d_->device->Content(content.GetAddressOf());
  if (SUCCEEDED(hr)) hr = content->Properties(properties.GetAddressOf());
  if (SUCCEEDED(hr)) hr = content->Transfer(resources.GetAddressOf());
  if (FAILED(hr)) {
    error_text_ = ErrorText(hr);
    error_kind_ = ErrorKindOf(hr);
    d_->device->Close();
    return false;
  }

  // Bulk reads, when the driver has them: one request for the properties of many objects.
  ComPtr<IPortableDevicePropertiesBulk> bulk;
  if (FAILED(properties.As(&bulk))) bulk.Reset();
  if (qEnvironmentVariableIsSet("AKOUSMA_WPD_NO_BULK")) bulk.Reset();

  {
    QMutexLocker l(&d_->mutex);
    d_->content = content;
    d_->properties = properties;
    d_->resources = resources;
    d_->bulk = bulk;
  }

  error_text_.clear();
  error_kind_ = ErrorKind::None;

  if (timer.elapsed() > 1000) qLog(Warning) << "Opening WPD device" << pnp_device_id_ << "took" << timer.elapsed() << "ms";

  return true;

}

bool WpdConnection::is_valid() const { return d_->device && d_->content && d_->properties && d_->resources; }

void WpdConnection::Cancel() {

  d_->cancelled = true;

  ComPtr<IPortableDevice> device;
  ComPtr<IPortableDeviceContent> content;
  ComPtr<IPortableDeviceProperties> properties;
  ComPtr<IPortableDeviceResources> resources;
  ComPtr<IEnumPortableDeviceObjectIDs> enumerator;
  ComPtr<IPortableDeviceDataStream> stream;
  QString operation;
  {
    QMutexLocker l(&d_->mutex);
    device = d_->device;
    content = d_->content;
    properties = d_->properties;
    resources = d_->resources;
    enumerator = d_->current_enum;
    stream = d_->current_stream;
    operation = d_->current_operation;
  }

  qLog(Info) << "Cancelling WPD device" << pnp_device_id_ << (operation.isEmpty() ? u"(no call running)"_s : u"while in "_s + operation);

  // Each interface cancels its own pending calls. Calling these with nothing pending does nothing.
  if (enumerator) enumerator->Cancel();
  if (stream) stream->Cancel();
  if (properties) properties->Cancel();
  if (content) content->Cancel();
  if (resources) resources->Cancel();
  if (device) device->Cancel();

}

bool WpdConnection::cancelled() const { return d_->cancelled; }

QString WpdConnection::CurrentOperation(qint64 *elapsed_msec) const {

  QMutexLocker l(&d_->mutex);
  if (elapsed_msec) *elapsed_msec = d_->current_operation.isEmpty() ? 0 : d_->current_operation_timer.elapsed();
  return d_->current_operation;

}

QString WpdConnection::ApartmentName() {

  APTTYPE type = APTTYPE_CURRENT;
  APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
  const HRESULT hr = CoGetApartmentType(&type, &qualifier);
  if (hr == CO_E_NOTINITIALIZED) return u"none"_s;
  if (FAILED(hr)) return u"unknown"_s;
  switch (type) {
    case APTTYPE_MTA: return qualifier == APTTYPEQUALIFIER_IMPLICIT_MTA ? u"implicit MTA"_s : u"MTA"_s;
    case APTTYPE_MAINSTA: return u"main STA"_s;
    case APTTYPE_STA: return u"STA"_s;
    case APTTYPE_NA: return u"NA"_s;
    default: return u"unknown"_s;
  }

}

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

  if (cancelled()) return ret;

  Private::Operation operation(d_.get(), u"reading the storages"_s);

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
  if (!is_valid() || cancelled()) return ret;

  Private::Operation operation(d_.get(), u"reading the supported formats"_s);

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

bool WpdConnection::Private::Children(const QString &parent_id, const QString &path, QStringList *ret) {

  if (cancelled) return false;

  Operation operation(this, u"listing "_s + path);

  ComPtr<IEnumPortableDeviceObjectIDs> objects;
  const HRESULT hr = content->EnumObjects(0, W(parent_id), nullptr, objects.GetAddressOf());
  if (FAILED(hr) || !objects) {
    if (cancelled) return false;
    qLogCat(Warning, kLogCategory) << "Could not list the objects in" << path << ErrorText(hr);
    return true;
  }
  {
    QMutexLocker l(&mutex);
    current_enum = objects;
  }
  const auto clear_enum = qScopeGuard([this]() {
    QMutexLocker l(&mutex);
    current_enum.Reset();
  });

  while (!cancelled) {
    LPWSTR ids[kEnumBatchSize] = {};
    ULONG fetched = 0;
    const HRESULT next_hr = objects->Next(kEnumBatchSize, ids, &fetched);
    for (ULONG i = 0; i < fetched; ++i) *ret << TakeWide(ids[i]);
    if (next_hr != S_OK || fetched == 0) {
      if (FAILED(next_hr) && !cancelled) qLogCat(Warning, kLogCategory) << "Could not list all objects in" << path << ErrorText(next_hr);
      break;
    }
  }

  return !cancelled;

}

QStringList WpdConnection::Private::Children(const QString &parent_id) {

  QStringList ret;
  Children(parent_id, parent_id, &ret);
  return ret;

}

WpdConnection::Private::BulkResult WpdConnection::Private::ReadBulk(const QStringList &object_ids, IPortableDeviceKeyCollection *keys, const QString &path, ValuesMap *ret) {

  ComPtr<IPortableDevicePropVariantCollection> collection = MakeObjectIds(object_ids);
  if (!collection) return BulkResult::Failed;

  ComPtr<BulkCallback> callback;
  callback.Attach(new BulkCallback);
  if (!callback->done()) return BulkResult::Failed;

  Operation operation(this, u"reading the properties of %1 objects in bulk in %2"_s.arg(object_ids.count()).arg(path));

  GUID context = {};
  HRESULT hr = bulk->QueueGetValuesByObjectList(collection.Get(), keys, callback.Get(), &context);
  if (FAILED(hr)) {
    qLogCat(Info, kLogCategory) << "The device doesn't read properties in bulk (QueueGetValuesByObjectList):" << ErrorText(hr);
    return BulkResult::Unsupported;
  }
  hr = bulk->Start(context);
  if (FAILED(hr)) {
    qLogCat(Info, kLogCategory) << "The device doesn't read properties in bulk (Start):" << ErrorText(hr);
    return BulkResult::Unsupported;
  }

  // Waits for OnEnd(). The thread is in the multithreaded apartment, so the callback comes on WPD's own thread and
  // nothing needs to be pumped here.
  QElapsedTimer idle;
  idle.start();
  LONG ticks = 0;
  BulkResult result = BulkResult::Ok;
  while (WaitForSingleObject(callback->done(), kBulkWaitStepMsec) != WAIT_OBJECT_0) {
    if (cancelled) {
      result = BulkResult::Cancelled;
      break;
    }
    const LONG new_ticks = callback->progress_ticks();
    if (new_ticks != ticks) {
      ticks = new_ticks;
      idle.restart();
    }
    else if (idle.elapsed() > kBulkIdleTimeoutMsec) {
      qLogCat(Warning, kLogCategory) << "Reading properties in bulk in" << path << "got no answer for" << kBulkIdleTimeoutMsec / 1000 << "seconds, cancelling it";
      result = BulkResult::Failed;
      break;
    }
  }
  if (result != BulkResult::Ok) {
    bulk->Cancel(context);
    if (WaitForSingleObject(callback->done(), kBulkCancelWaitMsec) != WAIT_OBJECT_0) {
      qLogCat(Warning, kLogCategory) << "Cancelling the bulk read in" << path << "didn't finish in" << kBulkCancelWaitMsec / 1000 << "seconds";
    }
    if (result == BulkResult::Cancelled) return result;
  }
  else if (FAILED(callback->status())) {
    qLogCat(Warning, kLogCategory) << "Reading properties in bulk in" << path << "failed:" << ErrorText(callback->status());
    result = BulkResult::Failed;
  }

  // Whatever came is used, also when it failed half way; the rest is read one at a time.
  const QList<ComPtr<IPortableDeviceValues>> results = callback->TakeResults();
  int matched = 0;
  for (const ComPtr<IPortableDeviceValues> &values : results) {
    const QString object_id = GetString(values.Get(), WPD_OBJECT_ID);
    if (object_id.isEmpty()) continue;
    ret->insert(object_id, values);
    ++matched;
  }
  if (result == BulkResult::Ok && !results.isEmpty() && matched == 0) {
    qLogCat(Warning, kLogCategory) << "Bulk results without object IDs, reading one object at a time from now on";
    return BulkResult::Unsupported;
  }

  return result;

}

bool WpdConnection::Private::ReadEach(const QStringList &object_ids, IPortableDeviceKeyCollection *keys, const QString &path, ValuesMap *ret) {

  int failed = 0;
  HRESULT last_error = S_OK;
  for (const QString &object_id : object_ids) {
    if (cancelled) return false;
    if (ret->contains(object_id)) continue;
    ComPtr<IPortableDeviceValues> values;
    HRESULT hr = E_FAIL;
    {
      Operation operation(this, u"reading the properties of %1 in %2"_s.arg(object_id, path));
      hr = properties->GetValues(W(object_id), keys, values.GetAddressOf());
    }
    if (FAILED(hr) || !values) {
      ++failed;
      last_error = hr;
      continue;
    }
    ret->insert(object_id, values);
  }
  if (cancelled) return false;
  if (failed > 0) qLogCat(Warning, kLogCategory) << "Could not read" << failed << "of" << object_ids.count() << "objects in" << path << ErrorText(last_error);
  return true;

}

bool WpdConnection::Private::ReadValues(const QStringList &object_ids, IPortableDeviceKeyCollection *keys, const QString &path, ValuesMap *ret) {

  if (object_ids.isEmpty()) return !cancelled;

  // The bulk callback comes on another thread; in a single threaded apartment it would need a message loop here.
  APTTYPE apartment = APTTYPE_CURRENT;
  APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
  const bool mta = SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) && apartment == APTTYPE_MTA;

  if (bulk && bulk_usable && mta) {
    for (qsizetype i = 0; i < object_ids.count() && bulk_usable; i += kBulkChunk) {
      const BulkResult result = ReadBulk(object_ids.mid(i, kBulkChunk), keys, path, ret);
      if (result == BulkResult::Cancelled) return false;
      if (result == BulkResult::Unsupported) {
        bulk_usable = false;
        qLogCat(Info, kLogCategory) << "Reading properties one object at a time";
      }
      else if (result == BulkResult::Failed && ++bulk_failures >= kMaxBulkFailures) {
        bulk_usable = false;
        qLogCat(Warning, kLogCategory) << "Reading properties in bulk failed" << bulk_failures << "times, reading one object at a time from now on";
      }
    }
  }

  // Everything the bulk read didn't return.
  return ReadEach(object_ids, keys, path, ret);

}

bool WpdConnection::Private::Walk(const QString &parent_id, const QString &path, const int depth, const WpdMetadata::ProgressSpan &span, WalkState *state) {

  if (cancelled) return false;
  if (depth > kMaxFolderDepth) return true;

  QElapsedTimer timer;
  timer.start();

  QStringList children;
  if (!Children(parent_id, path, &children)) return false;
  ++state->progress.folders;
  state->progress.objects += static_cast<int>(children.count());

  // Only what tells folders, songs and other files apart, for every object.
  ValuesMap object_values;
  if (!ReadValues(children, state->object_keys.Get(), path, &object_values)) return false;

  QStringList folder_ids;
  QStringList folder_names;
  QList<WpdMetadata::ObjectInfo> songs;
  QStringList song_ids;
  for (const QString &object_id : std::as_const(children)) {
    IPortableDeviceValues *values = object_values.value(object_id).Get();
    if (!values) {
      ++state->unreadable;
      continue;
    }

    const QUuid content_type = GetGuid(values, WPD_OBJECT_CONTENT_TYPE);
    const QUuid format = GetGuid(values, WPD_OBJECT_FORMAT);
    const QString name = GetString(values, WPD_OBJECT_NAME);
    const QString original_file_name = GetString(values, WPD_OBJECT_ORIGINAL_FILE_NAME);

    if (IsFolder(content_type, format)) {
      const QString folder_name = original_file_name.isEmpty() ? name : original_file_name;
      if (WpdMetadata::SkipFolder(folder_name, depth)) {
        ++state->progress.folders_skipped;
        qLogCat(Info, kLogCategory) << "Leaving out" << path + u'/' + folder_name;
        continue;
      }
      folder_ids << object_id;
      folder_names << folder_name;
      continue;
    }

    WpdMetadata::ObjectInfo info;
    info.object_id = object_id;
    info.name = name;
    info.original_file_name = original_file_name;
    info.format = format;
    info.audio_content_type = content_type == QUuid(WPD_CONTENT_TYPE_AUDIO);
    if (!WpdMetadata::IsAudioObject(info)) continue;
    songs << info;
    song_ids << object_id;
  }

  // The metadata, only for songs.
  if (!songs.isEmpty()) {
    ValuesMap song_values;
    if (!ReadValues(song_ids, state->song_keys.Get(), path, &song_values)) return false;
    for (WpdMetadata::ObjectInfo &info : songs) {
      if (IPortableDeviceValues *values = song_values.value(info.object_id).Get()) FillSongInfo(values, &info);
      if (cancelled || !state->callback(info)) return false;
    }
  }

  const int folder_count = static_cast<int>(folder_ids.count());
  state->progress.fraction = span.Part(-1, folder_count).end;
  if (state->progress_callback) state->progress_callback(state->progress);

  if (depth == 0 || timer.elapsed() > kSlowFolderMsec) {
    qLogCat(Info, kLogCategory) << "Folder" << path << ":" << children.count() << "files and folders," << songs.count() << "songs," << folder_count << "folders to look into," << timer.elapsed() << "ms";
  }

  for (int i = 0; i < folder_count; ++i) {
    if (!Walk(folder_ids[i], path + u'/' + folder_names[i], depth + 1, span.Part(i, folder_count), state)) return false;
  }

  return true;

}

bool WpdConnection::ListSongs(const ObjectFunction &callback, const WalkProgressFunction &progress) {

  if (!is_valid()) return false;

  Private::WalkState state;
  state.callback = callback;
  state.progress_callback = progress;
  // Every object: what it is and its name. Songs: their metadata. WPD_OBJECT_ID is how bulk results tell the objects apart.
  state.object_keys = MakeKeys({ WPD_OBJECT_ID, WPD_OBJECT_CONTENT_TYPE, WPD_OBJECT_FORMAT, WPD_OBJECT_NAME, WPD_OBJECT_ORIGINAL_FILE_NAME });
  state.song_keys = MakeKeys({ WPD_OBJECT_ID, WPD_OBJECT_SIZE, WPD_OBJECT_DATE_MODIFIED, WPD_MEDIA_TITLE, WPD_MEDIA_ARTIST, WPD_MEDIA_ALBUM_ARTIST,
                               WPD_MUSIC_ALBUM, WPD_MEDIA_GENRE, WPD_MEDIA_COMPOSER, WPD_MUSIC_TRACK, WPD_MEDIA_RELEASE_DATE,
                               WPD_MEDIA_DURATION, WPD_MEDIA_SAMPLE_RATE, WPD_AUDIO_BITRATE, WPD_AUDIO_BIT_DEPTH, WPD_MEDIA_USE_COUNT });
  if (!state.object_keys || !state.song_keys) return false;

  const QList<StorageInfo> storages = Storages();
  qLog(Info) << "Reading the songs on" << storages.count() << "storages of" << pnp_device_id_ << "in a" << ApartmentName() << "thread,"
             << (d_->bulk && d_->bulk_usable ? "properties in bulk" : "properties one object at a time");

  QElapsedTimer timer;
  timer.start();

  const int storage_count = static_cast<int>(storages.count());
  for (int i = 0; i < storage_count; ++i) {
    const StorageInfo &storage = storages[i];
    WpdMetadata::ProgressSpan span;
    span.begin = static_cast<double>(i) / static_cast<double>(storage_count);
    span.end = static_cast<double>(i + 1) / static_cast<double>(storage_count);
    const QString path = storage.description.isEmpty() ? storage.object_id : storage.description;
    qLog(Info) << "Reading storage" << path << "(" << storage.object_id << ")";
    if (!d_->Walk(storage.object_id, path, 0, span, &state)) {
      qLog(Info) << (cancelled() ? "Cancelled" : "Stopped") << "after" << state.progress.folders << "folders," << state.progress.objects << "files and folders," << timer.elapsed() << "ms";
      return false;
    }
  }

  qLog(Info) << "Walked" << state.progress.folders << "folders (" << state.progress.folders_skipped << "left out )," << state.progress.objects << "files and folders,"
             << state.unreadable << "unreadable, in" << timer.elapsed() << "ms";

  return true;

}

bool WpdConnection::DownloadHead(const QString &object_id, const QString &filename, const qint64 max_bytes) {

  if (!is_valid() || cancelled()) return false;

  Private::Operation operation(d_.get(), u"reading the start of "_s + object_id);

  DWORD buffer_size = 0;
  ComPtr<IStream> stream;
  const HRESULT hr = d_->resources->GetStream(W(object_id), WPD_RESOURCE_DEFAULT, STGM_READ, &buffer_size, stream.GetAddressOf());
  if (FAILED(hr) || !stream) {
    error_text_ = ErrorText(hr);
    return false;
  }

  QFile file(filename);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

  {
    ComPtr<IPortableDeviceDataStream> data_stream;
    if (FAILED(stream.As(&data_stream))) data_stream.Reset();
    QMutexLocker l(&d_->mutex);
    d_->current_stream = data_stream;
  }
  const auto clear_stream = qScopeGuard([this]() {
    QMutexLocker l(&d_->mutex);
    d_->current_stream.Reset();
  });

  buffer_size = qBound(static_cast<DWORD>(4096), buffer_size == 0 ? kDefaultBufferSize : buffer_size, kMaxBufferSize);
  QByteArray buffer(static_cast<qsizetype>(buffer_size), Qt::Uninitialized);
  qint64 total = 0;
  while (total < max_bytes && !cancelled()) {
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
