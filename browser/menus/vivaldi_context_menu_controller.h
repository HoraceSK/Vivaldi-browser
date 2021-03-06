//
// Copyright (c) 2019 Vivaldi Technologies AS. All rights reserved.
//
#ifndef BROWSER_MENUS_CONTEXT_MENU_CONTROLLER_H_
#define BROWSER_MENUS_CONTEXT_MENU_CONTROLLER_H_

#include <map>
#include <vector>

#include "base/task/cancelable_task_tracker.h"
#include "base/timer/timer.h"
#include "browser/menus/vivaldi_developertools_menu_controller.h"
#include "browser/menus/vivaldi_pwa_menu_controller.h"
#include "extensions/schema/context_menu.h"
#include "ui/base/models/simple_menu_model.h"

class Browser;

namespace content {
class WebContents;
}

namespace favicon {
class FaviconService;
}

namespace favicon_base {
struct FaviconImageResult;
}

namespace vivaldi {
class VivaldiContextMenu;


class ContextMenuPostitionDelegate {
 public:
  virtual void SetPosition(gfx::Rect* menu_bounds,
                           const gfx::Rect& monitor_bounds,
                           const gfx::Rect& anchor_bounds) const {}
};

class ContextMenuController : public ui::SimpleMenuModel::Delegate,
                              public ContextMenuPostitionDelegate {
 public:
  using MenuItem = extensions::vivaldi::context_menu::MenuItem;
  using Params = extensions::vivaldi::context_menu::Show::Params;

  class Delegate {
   public:
    virtual void OnAction(int command, int event_state) {}
    virtual void OnHover(const std::string& url) {}
    virtual void OnOpened() {}
    virtual void OnClosed() {}
  };

  ContextMenuController(Delegate* delegate,
                        content::WebContents* web_contents,
                        std::unique_ptr<Params> params);
  ~ContextMenuController() override;

  void Show();

  // ui::SimpleMenuModel::Delegate
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsItemForCommandIdDynamic(int command_id) const override;
  base::string16 GetLabelForCommandId(int command_id) const override;
  bool GetAcceleratorForCommandId(int command_id,
                                  ui::Accelerator* accelerator) const override;
  void VivaldiCommandIdHighlighted(int command_id) override;
  void ExecuteCommand(int command_id, int event_flags) override;
  void MenuClosed(ui::SimpleMenuModel* source) override;

  // ContextMenuPostitionDelegate
  void SetPosition(gfx::Rect* menu_bounds,
                   const gfx::Rect& monitor_bounds,
                   const gfx::Rect& anchor_bounds) const override;

 private:
  void InitModel();
  void PopulateModel(const MenuItem& menuitem, ui::SimpleMenuModel* menu_model);
  void SanitizeModel(ui::SimpleMenuModel* menu_model);
  void LoadFavicon(int command_id, const std::string& url);
  void OnFaviconDataAvailable(
      int command_id,
      const favicon_base::FaviconImageResult& image_result);
  void DelayedClose();

  typedef std::map<int, bool> IdToBoolMap;
  typedef std::map<int, std::string*> IdToStringMap;

  Delegate* delegate_;
  content::WebContents* web_contents_;  // Not owned by us.
  Browser* browser_;
  std::unique_ptr<Params> params_;

  // Loading favicons
  base::CancelableTaskTracker cancelable_task_tracker_;
  favicon::FaviconService* favicon_service_ = nullptr;

  ui::SimpleMenuModel* menu_model_ = nullptr;
  std::vector<std::unique_ptr<ui::SimpleMenuModel>> models_;
  std::unique_ptr<VivaldiContextMenu> menu_;
  IdToStringMap id_to_url_map_;
  IdToBoolMap id_to_checked_map_;
  gfx::Rect rect_;

  std::unique_ptr<DeveloperToolsMenuController> developertools_controller_;
  std::unique_ptr<PWAMenuController> pwa_controller_;
  std::unique_ptr<base::OneShotTimer> timer_;
};

}  // namespace vivaldi

#endif  // BROWSER_MENUS_CONTEXT_MENU_CONTROLLER_H_
