//
// Copyright (c) 2015 Vivaldi Technologies AS. All rights reserved.
//

#ifndef EXTENSIONS_API_VIVALDI_UTILITIES_VIVALDI_UTILITIES_API_H_
#define EXTENSIONS_API_VIVALDI_UTILITIES_VIVALDI_UTILITIES_API_H_

#include <map>
#include <memory>
#include <string>

#include "base/lazy_instance.h"
#include "base/power_monitor/power_observer.h"
#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "chrome/browser/password_manager/reauth_purpose.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/passwords/settings/password_access_authenticator.h"
#include "content/public/browser/download_manager.h"
#include "extensions/browser/api/file_system/file_system_api.h"
#include "extensions/browser/app_window/app_window.h"
#include "extensions/browser/app_window/app_window_registry.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_function.h"
#include "extensions/schema/vivaldi_utilities.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/lights/razer_chroma_handler.h"

class Browser;

namespace extensions {

class VivaldiUtilitiesAPI : public BrowserContextKeyedAPI,
                            public EventRouter::Observer,
                            public base::PowerObserver,
                            public content::DownloadManager::Observer {
 public:
  explicit VivaldiUtilitiesAPI(content::BrowserContext* context);
  ~VivaldiUtilitiesAPI() override;

  void PostProfileSetup();

  // KeyedService implementation.
  void Shutdown() override;

  // BrowserContextKeyedAPI implementation.
  static BrowserContextKeyedAPIFactory<VivaldiUtilitiesAPI>*
  GetFactoryInstance();

  // EventRouter::Observer implementation.
  void OnListenerAdded(const EventListenerInfo& details) override;

  // Fires an event to JS with the active device that triggers scrolling.
  // 1: Mouse, 2: Trackpad 3: Inertial"
  static void ScrollType(content::BrowserContext* browser_context,
                         int scrollType);

  // Returns true if the key didn't not exist previously, false if it updated
  // an existing value
  bool SetSharedData(const std::string& key, base::Value* value);

  // Looks up an existing key/value pair, returns nullptr if the key does not
  // exist.
  const base::Value* GetSharedData(const std::string& key);

  // Sets anchor rect for the named dialog. Returns true if the key didn't not
  // exist previously, false if it updated an existing value
  bool SetDialogPostion(int window_id,
                        const std::string& dialog,
                        const gfx::Rect& rect,
                        const std::string& flow_direction);

  // Sets anchor rect for the named dialog
  gfx::Rect GetDialogPosition(int window_id,
                              const std::string& dialog,
                              std::string* flow_direction);

  // PowerObserver implementation
  void OnPowerStateChange(bool on_battery_power) override;
  void OnSuspend() override;
  void OnResume() override;

  // DownloadManager::Observer implementation
  void OnManagerInitialized() override;
  void ManagerGoingDown(content::DownloadManager* manager) override;

  void OnPasswordIconStatusChanged(int window_id, bool show);

  // Trigger the OS authentication dialog, if needed. web_contents can be null.
  static bool AuthenticateUser(content::BrowserContext* browser_context,
                               content::WebContents* web_contents);

  // Is the Razer Chroma API available on this machine
  bool IsRazerChromaAvailable();

  // Is the Razer Chroma API ready to accept commands.
  bool IsRazerChromaReady();

  // Set RGB color of the configured Razer Chroma devices.
  bool SetRazerChromaColors(RazerChromaColors& colors);

  class DialogPosition {
   public:
    DialogPosition(int window_id,
                   const std::string& dialog_name,
                   gfx::Rect rect,
                   const std::string& flow_direction);

    ~DialogPosition() {}

    int window_id() { return window_id_; }
    const std::string& dialog_name() { return dialog_name_; }
    const gfx::Rect& rect() { return rect_; }
    const std::string& flow_direction() { return flow_direction_;  }

   private:
    int window_id_ = 0;
    std::string dialog_name_;
    gfx::Rect rect_;
    std::string flow_direction_;
  };
 protected:
  bool OsReauthCall(password_manager::ReauthPurpose purpose);

 private:
  friend class BrowserContextKeyedAPIFactory<VivaldiUtilitiesAPI>;

  content::BrowserContext* browser_context_;

  // BrowserContextKeyedAPI implementation.
  static const char* service_name() { return "UtilitiesAPI"; }
  static const bool kServiceIsNULLWhileTesting = true;
  static const bool kServiceRedirectedInIncognito = true;

  // Map used for the *sharedData apis.
  std::map<std::string, base::Value*> key_to_values_map_;

  // List used for the dialog position apis.
  std::vector<std::unique_ptr<DialogPosition> > dialog_to_point_list_;

  // Persistent class used for re-authentication of the user when viewing
  // saved passwords. It cannot be instanciated per call as it keeps state
  // of previous authentiations.
  PasswordAccessAuthenticator password_access_authenticator_;

  // Used to anchor the auth dialog.
  gfx::NativeWindow native_window_ = nullptr;

  // Razer Chroma integration, if available.
  std::unique_ptr<RazerChromaHandler> razer_chroma_handler_;
};

class UtilitiesPrintFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.print", UTILITIES_PRINT)
  UtilitiesPrintFunction() = default;

  ResponseAction Run() override;

 protected:
  ~UtilitiesPrintFunction() override = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesPrintFunction);
};

class UtilitiesClearAllRecentlyClosedSessionsFunction
    : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.clearAllRecentlyClosedSessions",
                             UTILITIES_CLEARALLRECENTLYCLOSEDSESSIONS)
  UtilitiesClearAllRecentlyClosedSessionsFunction() = default;

 protected:
  ~UtilitiesClearAllRecentlyClosedSessionsFunction() override = default;

  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesClearAllRecentlyClosedSessionsFunction);
};

class UtilitiesIsTabInLastSessionFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.isTabInLastSession",
                             UTILITIES_ISTABINLASTSESSION)
  UtilitiesIsTabInLastSessionFunction() = default;

 protected:
  ~UtilitiesIsTabInLastSessionFunction() override = default;

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesIsTabInLastSessionFunction);
};

class UtilitiesIsUrlValidFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.isUrlValid", UTILITIES_ISURLVALID)
  UtilitiesIsUrlValidFunction();

 protected:
  ~UtilitiesIsUrlValidFunction() override;

  void OnDefaultProtocolClientWorkerFinished(
      shell_integration::DefaultWebClientState state);

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  bool prompt_user_ = false;
  GURL url_;
  vivaldi::utilities::UrlValidResults result_;

  DISALLOW_COPY_AND_ASSIGN(UtilitiesIsUrlValidFunction);
};

class UtilitiesGetSelectedTextFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.getSelectedText",
                             UTILITIES_GETSELECTEDTEXT)
  UtilitiesGetSelectedTextFunction() = default;

 protected:
  ~UtilitiesGetSelectedTextFunction() override = default;

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesGetSelectedTextFunction);
};

class UtilitiesSelectFileFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.selectFile", UTILITIES_SELECTFILE)
  UtilitiesSelectFileFunction() = default;

 private:
  ~UtilitiesSelectFileFunction() override = default;

  // ExtensionFunction:
  ResponseAction Run() override;

  void OnFileSelected(base::FilePath path);

  DISALLOW_COPY_AND_ASSIGN(UtilitiesSelectFileFunction);
};

class UtilitiesSelectLocalImageFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.selectLocalImage",
                             UTILITIES_SELECTLOCALIMAGE)
  UtilitiesSelectLocalImageFunction() = default;

 private:
  ~UtilitiesSelectLocalImageFunction() override = default;

  // ExtensionFunction:
  ResponseAction Run() override;

  void OnFileSelected(int64_t bookmark_id,
                      int preference_index,
                      base::FilePath path);

  void SendResult(bool success);

  DISALLOW_COPY_AND_ASSIGN(UtilitiesSelectLocalImageFunction);
};

class UtilitiesGetVersionFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.getVersion",
                             UTILITIES_GETVERSION)
  UtilitiesGetVersionFunction() = default;

 protected:
  ~UtilitiesGetVersionFunction() override = default;

  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesGetVersionFunction);
};

class UtilitiesGetFFMPEGStateFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.getFFMPEGState",
                             UTILITIES_GET_FFMPEG_STATE)
  UtilitiesGetFFMPEGStateFunction() = default;

 protected:
  ~UtilitiesGetFFMPEGStateFunction() override = default;

  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesGetFFMPEGStateFunction);
};

class UtilitiesSetSharedDataFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.setSharedData", UTILITIES_SETSHAREDDATA)

  UtilitiesSetSharedDataFunction() = default;

 protected:
  ~UtilitiesSetSharedDataFunction() override = default;

  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesSetSharedDataFunction);
};

class UtilitiesGetSharedDataFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.getSharedData", UTILITIES_GETSHAREDDATA)

  UtilitiesGetSharedDataFunction() = default;

 protected:
  ~UtilitiesGetSharedDataFunction() override = default;

  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesGetSharedDataFunction);
};

class UtilitiesGetSystemDateFormatFunction : public ExtensionFunction {
  DECLARE_EXTENSION_FUNCTION("utilities.getSystemDateFormat",
                             UTILITIES_GETSYSTEM_DATE_FORMAT)
 public:
  UtilitiesGetSystemDateFormatFunction() = default;

 protected:
  ~UtilitiesGetSystemDateFormatFunction() override = default;

  bool ReadDateFormats(vivaldi::utilities::DateFormats* date_formats);

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesGetSystemDateFormatFunction);
};

class UtilitiesGetSystemCountryFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.getSystemCountry",
                             UTILITIES_GET_SYSTEM_COUNTRY)
  UtilitiesGetSystemCountryFunction() = default;

 private:
  ~UtilitiesGetSystemCountryFunction() override = default;

  std::string ReadCountry();

  ResponseAction Run() override;

  DISALLOW_COPY_AND_ASSIGN(UtilitiesGetSystemCountryFunction);
};

class UtilitiesSetLanguageFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.setLanguage", UTILITIES_SETLANGUAGE)
  UtilitiesSetLanguageFunction() = default;

 protected:
  ~UtilitiesSetLanguageFunction() override = default;
  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesSetLanguageFunction);
};

class UtilitiesGetLanguageFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.getLanguage", UTILITIES_GETLANGUAGE)
  UtilitiesGetLanguageFunction() = default;

 protected:
  ~UtilitiesGetLanguageFunction() override = default;
  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesGetLanguageFunction);
};

class UtilitiesSetVivaldiAsDefaultBrowserFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.setVivaldiAsDefaultBrowser",
                             UTILITIES_SETVIVALDIDEFAULT)
  UtilitiesSetVivaldiAsDefaultBrowserFunction() = default;

 protected:
  ~UtilitiesSetVivaldiAsDefaultBrowserFunction() override;

  void OnDefaultBrowserWorkerFinished(
      shell_integration::DefaultWebClientState state);

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesSetVivaldiAsDefaultBrowserFunction);
};

class UtilitiesIsVivaldiDefaultBrowserFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.isVivaldiDefaultBrowser",
                             UTILITIES_ISVIVALDIDEFAULT)
  UtilitiesIsVivaldiDefaultBrowserFunction() = default;

 protected:
  ~UtilitiesIsVivaldiDefaultBrowserFunction() override;

  void OnDefaultBrowserWorkerFinished(
      shell_integration::DefaultWebClientState state);

  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesIsVivaldiDefaultBrowserFunction);
};

class UtilitiesLaunchNetworkSettingsFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.launchNetworkSettings",
                             UTILITIES_LAUNCHNETWORKSETTINGS)
  UtilitiesLaunchNetworkSettingsFunction() = default;

 protected:
  ~UtilitiesLaunchNetworkSettingsFunction() override = default;
  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesLaunchNetworkSettingsFunction);
};

class UtilitiesSavePageFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.savePage", UTILITIES_SAVEPAGE)
  UtilitiesSavePageFunction() = default;

 protected:
  ~UtilitiesSavePageFunction() override = default;
  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesSavePageFunction);
};

class UtilitiesOpenPageFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.openPage",
                             UTILITIES_OPENPAGE)
  UtilitiesOpenPageFunction() = default;

 protected:
  ~UtilitiesOpenPageFunction() override = default;
  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesOpenPageFunction);
};

class UtilitiesSetDefaultContentSettingsFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.setDefaultContentSettings",
                             UTILITIES_SETDEFAULTCONTENTSETTING)
  UtilitiesSetDefaultContentSettingsFunction() = default;

 protected:
  ~UtilitiesSetDefaultContentSettingsFunction() override = default;
  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesSetDefaultContentSettingsFunction);
};

class UtilitiesGetDefaultContentSettingsFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.getDefaultContentSettings",
                             UTILITIES_GETDEFAULTCONTENTSETTING)
  UtilitiesGetDefaultContentSettingsFunction() = default;

 protected:
  ~UtilitiesGetDefaultContentSettingsFunction() override = default;
  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesGetDefaultContentSettingsFunction);
};

class UtilitiesSetBlockThirdPartyCookiesFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.setBlockThirdPartyCookies",
                             UTILITIES_SET_BLOCKTHIRDPARTYCOOKIES)
  UtilitiesSetBlockThirdPartyCookiesFunction() = default;

 protected:
  ~UtilitiesSetBlockThirdPartyCookiesFunction() override = default;
  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesSetBlockThirdPartyCookiesFunction);
};

class UtilitiesGetBlockThirdPartyCookiesFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.getBlockThirdPartyCookies",
                             UTILITIES_GET_BLOCKTHIRDPARTYCOOKIES)
  UtilitiesGetBlockThirdPartyCookiesFunction() = default;

 protected:
  ~UtilitiesGetBlockThirdPartyCookiesFunction() override = default;
  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesGetBlockThirdPartyCookiesFunction);
};

class UtilitiesOpenTaskManagerFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.openTaskManager",
                             UTILITIES_OPENTASKMANAGER)
  UtilitiesOpenTaskManagerFunction() = default;

 protected:
  ~UtilitiesOpenTaskManagerFunction() override = default;
  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesOpenTaskManagerFunction);
};


class UtilitiesGetStartupActionFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.getStartupAction",
                             UTILITIES_GET_STARTUPTYPE)
  UtilitiesGetStartupActionFunction() = default;

 protected:
  ~UtilitiesGetStartupActionFunction() override = default;
  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesGetStartupActionFunction);
};

class UtilitiesSetStartupActionFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.setStartupAction",
                             UTILITIES_SET_STARTUPTYPE)
  UtilitiesSetStartupActionFunction() = default;

 protected:
  ~UtilitiesSetStartupActionFunction() override = default;
  // ExtensionFunction:
  ResponseAction Run() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesSetStartupActionFunction);
};

class UtilitiesCanShowWhatsNewPageFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.canShowWhatsNewPage",
                             UTILITIES_CANSHOWWHATSNEWPAGE)
  UtilitiesCanShowWhatsNewPageFunction() = default;

  ResponseAction Run() override;

 protected:
  ~UtilitiesCanShowWhatsNewPageFunction() override = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(UtilitiesCanShowWhatsNewPageFunction);
};

class UtilitiesShowPasswordDialogFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.showPasswordDialog",
                             UTILITIES_SHOW_PASSWORD_DIALOG)
  UtilitiesShowPasswordDialogFunction() = default;

 private:
  ~UtilitiesShowPasswordDialogFunction() override = default;
  ResponseAction Run() override;

  DISALLOW_COPY_AND_ASSIGN(UtilitiesShowPasswordDialogFunction);
};

class UtilitiesSetDialogPositionFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.setDialogPosition",
                             UTILITIES_SET_DIALOG_POSITION)
  UtilitiesSetDialogPositionFunction() = default;

 private:
  ~UtilitiesSetDialogPositionFunction() override = default;
  ResponseAction Run() override;

  DISALLOW_COPY_AND_ASSIGN(UtilitiesSetDialogPositionFunction);
};

class UtilitiesIsRazerChromaAvailableFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.isRazerChromaAvailable",
                             UTILITIES_IS_RAZER_CHROMA_AVAILABLE)
  UtilitiesIsRazerChromaAvailableFunction() = default;

 private:
  ~UtilitiesIsRazerChromaAvailableFunction() override = default;
  ResponseAction Run() override;

  DISALLOW_COPY_AND_ASSIGN(UtilitiesIsRazerChromaAvailableFunction);
};

class UtilitiesIsRazerChromaReadyFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.isRazerChromaReady",
                             UTILITIES_IS_RAZER_CHROMA_READY)
  UtilitiesIsRazerChromaReadyFunction() = default;

 private:
  ~UtilitiesIsRazerChromaReadyFunction() override = default;
  ResponseAction Run() override;

  DISALLOW_COPY_AND_ASSIGN(UtilitiesIsRazerChromaReadyFunction);
};

class UtilitiesSetRazerChromaColorFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.setRazerChromaColor",
                             UTILITIES_SET_RAZER_CHROMA_COLOR)
  UtilitiesSetRazerChromaColorFunction() = default;

 private:
  ~UtilitiesSetRazerChromaColorFunction() override = default;
  ResponseAction Run() override;

  DISALLOW_COPY_AND_ASSIGN(UtilitiesSetRazerChromaColorFunction);
};

class UtilitiesIsDownloadManagerReadyFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.isDownloadManagerReady",
                             UTILITIES_IS_DOWNLOAD_MANAGER_READY)
  UtilitiesIsDownloadManagerReadyFunction() = default;

 private:
  ~UtilitiesIsDownloadManagerReadyFunction() override = default;
  ResponseAction Run() override;

  DISALLOW_COPY_AND_ASSIGN(UtilitiesIsDownloadManagerReadyFunction);
};

class UtilitiesSetContentSettingsFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.setContentSettings",
                             UTILITIES_SET_CONTENTSETTING)
  UtilitiesSetContentSettingsFunction() = default;

 private:
  ~UtilitiesSetContentSettingsFunction() override = default;
  ResponseAction Run() override;

  DISALLOW_COPY_AND_ASSIGN(UtilitiesSetContentSettingsFunction);
};

class UtilitiesIsDialogOpenFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.isDialogOpen", UTILITIES_IS_DIALOG_OPEN)
  UtilitiesIsDialogOpenFunction() = default;

 private:
  ~UtilitiesIsDialogOpenFunction() override = default;
  ResponseAction Run() override;

  DISALLOW_COPY_AND_ASSIGN(UtilitiesIsDialogOpenFunction);
};

class UtilitiesFocusDialogFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("utilities.focusDialog", UTILITIES_FOCUS_DIALOG)
  UtilitiesFocusDialogFunction() = default;

 private:
  ~UtilitiesFocusDialogFunction() override = default;
  ResponseAction Run() override;

  DISALLOW_COPY_AND_ASSIGN(UtilitiesFocusDialogFunction);
};

}  // namespace extensions

#endif  // EXTENSIONS_API_VIVALDI_UTILITIES_VIVALDI_UTILITIES_API_H_
