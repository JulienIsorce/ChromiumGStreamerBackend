// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_context_menu_model.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/extensions/api/extension_action/extension_action_api.h"
#include "chrome/browser/extensions/extension_action_test_util.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_service_test_base.h"
#include "chrome/browser/extensions/menu_manager.h"
#include "chrome/browser/extensions/menu_manager_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/host_desktop.h"
#include "chrome/common/extensions/api/context_menus.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/test_browser_window.h"
#include "chrome/test/base/testing_profile.h"
#include "components/crx_file/id_util.h"
#include "extensions/browser/extension_dialog_auto_confirm.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/test_extension_registry_observer.h"
#include "extensions/browser/test_management_policy.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/feature_switch.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/manifest_handlers/options_page_info.h"
#include "extensions/common/value_builder.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/image/image.h"

namespace extensions {

namespace {

// Label for test extension menu item.
const char* kTestExtensionItemLabel = "test-ext-item";

// Build an extension to pass to the menu constructor, with the an action
// specified by |action_key|.
scoped_refptr<const Extension> BuildExtension(const std::string& name,
                                              const char* action_key,
                                              Manifest::Location location) {
  return ExtensionBuilder()
      .SetManifest(DictionaryBuilder()
                       .Set("name", name)
                       .Set("version", "1")
                       .Set("manifest_version", 2)
                       .Set(action_key, DictionaryBuilder().Pass()))
      .SetID(crx_file::id_util::GenerateId(name))
      .SetLocation(location)
      .Build();
}

class MenuBuilder {
 public:
  MenuBuilder(scoped_refptr<const Extension> extension,
              Browser* browser,
              MenuManager* menu_manager)
      : extension_(extension),
        browser_(browser),
        menu_manager_(menu_manager),
        cur_id_(0) {}
  ~MenuBuilder() {}

  scoped_ptr<ExtensionContextMenuModel> BuildMenu() {
    return make_scoped_ptr(
        new ExtensionContextMenuModel(extension_.get(), browser_));
  }

  void AddContextItem(MenuItem::Context context) {
    MenuItem::Id id(false /* not incognito */,
                    MenuItem::ExtensionKey(extension_->id()));
    id.uid = ++cur_id_;
    menu_manager_->AddContextItem(
        extension_.get(),
        new MenuItem(id, kTestExtensionItemLabel,
                     false,  // check`ed
                     true,   // enabled
                     MenuItem::NORMAL, MenuItem::ContextList(context)));
  }

 private:
  scoped_refptr<const Extension> extension_;
  Browser* browser_;
  MenuManager* menu_manager_;
  int cur_id_;

  DISALLOW_COPY_AND_ASSIGN(MenuBuilder);
};

// Returns the index of the given |command_id| in the given |menu|, or -1 if it
// is not found.
int GetCommandIndex(const ExtensionContextMenuModel& menu, int command_id) {
  int item_count = menu.GetItemCount();
  for (int i = 0; i < item_count; ++i) {
    if (menu.GetCommandIdAt(i) == command_id)
      return i;
  }
  return -1;
}

}  // namespace

class ExtensionContextMenuModelTest : public ExtensionServiceTestBase {
 public:
  ExtensionContextMenuModelTest();

  // Returns the number of extension menu items that show up in |model|.
  // For this test, all the extension items have samel label
  // |kTestExtensionItemLabel|.
  int CountExtensionItems(const ExtensionContextMenuModel& model);

  Browser* GetBrowser();

 private:
  scoped_ptr<TestBrowserWindow> test_window_;
  scoped_ptr<Browser> browser_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionContextMenuModelTest);
};

ExtensionContextMenuModelTest::ExtensionContextMenuModelTest() {}

int ExtensionContextMenuModelTest::CountExtensionItems(
    const ExtensionContextMenuModel& model) {
  base::string16 expected_label = base::ASCIIToUTF16(kTestExtensionItemLabel);
  int num_items_found = 0;
  int num_custom_found = 0;
  for (int i = 0; i < model.GetItemCount(); ++i) {
    if (expected_label == model.GetLabelAt(i))
      ++num_items_found;
    int command_id = model.GetCommandIdAt(i);
    if (command_id >= IDC_EXTENSIONS_CONTEXT_CUSTOM_FIRST &&
        command_id <= IDC_EXTENSIONS_CONTEXT_CUSTOM_LAST)
      ++num_custom_found;
  }
  // The only custom extension items present on the menu should be those we
  // added in the test.
  EXPECT_EQ(num_items_found, num_custom_found);
  return num_items_found;
}

Browser* ExtensionContextMenuModelTest::GetBrowser() {
  if (!browser_) {
    Browser::CreateParams params(profile(), chrome::GetActiveDesktop());
    test_window_.reset(new TestBrowserWindow());
    params.window = test_window_.get();
    browser_.reset(new Browser(params));
  }
  return browser_.get();
}

// Tests that applicable menu items are disabled when a ManagementPolicy
// prohibits them.
TEST_F(ExtensionContextMenuModelTest, RequiredInstallationsDisablesItems) {
  InitializeEmptyExtensionService();

  // Test that management policy can determine whether or not policy-installed
  // extensions can be installed/uninstalled.
  scoped_refptr<const Extension> extension =
      BuildExtension("extension",
                     manifest_keys::kPageAction,
                     Manifest::EXTERNAL_POLICY);
  ASSERT_TRUE(extension.get());
  service()->AddExtension(extension.get());

  ExtensionContextMenuModel menu(extension.get(), GetBrowser());

  ExtensionSystem* system = ExtensionSystem::Get(profile());
  system->management_policy()->UnregisterAllProviders();

  // Uninstallation should be, by default, enabled.
  EXPECT_TRUE(menu.IsCommandIdEnabled(ExtensionContextMenuModel::UNINSTALL));

  TestManagementPolicyProvider policy_provider(
      TestManagementPolicyProvider::PROHIBIT_MODIFY_STATUS);
  system->management_policy()->RegisterProvider(&policy_provider);

  // If there's a policy provider that requires the extension stay enabled, then
  // uninstallation should be disabled.
  EXPECT_FALSE(menu.IsCommandIdEnabled(ExtensionContextMenuModel::UNINSTALL));
  int uninstall_index =
      menu.GetIndexOfCommandId(ExtensionContextMenuModel::UNINSTALL);
  // There should also be an icon to visually indicate why uninstallation is
  // forbidden.
  gfx::Image icon;
  EXPECT_TRUE(menu.GetIconAt(uninstall_index, &icon));
  EXPECT_FALSE(icon.IsEmpty());

  // Don't leave |policy_provider| dangling.
  system->management_policy()->UnregisterProvider(&policy_provider);
}

// Tests the context menu for a component extension.
TEST_F(ExtensionContextMenuModelTest, ComponentExtensionContextMenu) {
  InitializeEmptyExtensionService();

  std::string name("component");
  scoped_ptr<base::DictionaryValue> manifest =
      DictionaryBuilder().Set("name", name)
                         .Set("version", "1")
                         .Set("manifest_version", 2)
                         .Set("browser_action", DictionaryBuilder().Pass())
                         .Build();

  {
    scoped_refptr<const Extension> extension =
        ExtensionBuilder()
            .SetManifest(make_scoped_ptr(manifest->DeepCopy()))
            .SetID(crx_file::id_util::GenerateId("component"))
            .SetLocation(Manifest::COMPONENT)
            .Build();
    service()->AddExtension(extension.get());

    ExtensionContextMenuModel menu(extension.get(), GetBrowser());

    // A component extension's context menu should not include options for
    // managing extensions or removing it, and should only include an option for
    // the options page if the extension has one (which this one doesn't).
    EXPECT_EQ(-1,
              menu.GetIndexOfCommandId(ExtensionContextMenuModel::CONFIGURE));
    EXPECT_EQ(-1,
              menu.GetIndexOfCommandId(ExtensionContextMenuModel::UNINSTALL));
    EXPECT_EQ(-1, menu.GetIndexOfCommandId(ExtensionContextMenuModel::MANAGE));
    // The "name" option should be present, but not enabled for component
    // extensions.
    EXPECT_NE(-1, menu.GetIndexOfCommandId(ExtensionContextMenuModel::NAME));
    EXPECT_FALSE(menu.IsCommandIdEnabled(ExtensionContextMenuModel::NAME));
  }

  {
    // Check that a component extension with an options page does have the
    // options
    // menu item, and it is enabled.
    manifest->SetString("options_page", "options_page.html");
    scoped_refptr<const Extension> extension =
        ExtensionBuilder()
            .SetManifest(manifest.Pass())
            .SetID(crx_file::id_util::GenerateId("component_opts"))
            .SetLocation(Manifest::COMPONENT)
            .Build();
    ExtensionContextMenuModel menu(extension.get(), GetBrowser());
    service()->AddExtension(extension.get());
    EXPECT_TRUE(extensions::OptionsPageInfo::HasOptionsPage(extension.get()));
    EXPECT_NE(-1,
              menu.GetIndexOfCommandId(ExtensionContextMenuModel::CONFIGURE));
    EXPECT_TRUE(menu.IsCommandIdEnabled(ExtensionContextMenuModel::CONFIGURE));
  }
}

TEST_F(ExtensionContextMenuModelTest, ExtensionItemTest) {
  InitializeEmptyExtensionService();
  scoped_refptr<const Extension> extension =
      BuildExtension("extension",
                     manifest_keys::kPageAction,
                     Manifest::INTERNAL);
  ASSERT_TRUE(extension.get());
  service()->AddExtension(extension.get());

  // Create a MenuManager for adding context items.
  MenuManager* manager = static_cast<MenuManager*>(
      (MenuManagerFactory::GetInstance()->SetTestingFactoryAndUse(
          profile(),
          &MenuManagerFactory::BuildServiceInstanceForTesting)));
  ASSERT_TRUE(manager);

  MenuBuilder builder(extension, GetBrowser(), manager);

  // There should be no extension items yet.
  EXPECT_EQ(0, CountExtensionItems(*builder.BuildMenu()));

  // Add a browser action menu item.
  builder.AddContextItem(MenuItem::BROWSER_ACTION);
  // Since |extension| has a page action, the browser action menu item should
  // not be present.
  EXPECT_EQ(0, CountExtensionItems(*builder.BuildMenu()));

  // Add a page action menu item. This should be present because |extension|
  // has a page action.
  builder.AddContextItem(MenuItem::PAGE_ACTION);
  EXPECT_EQ(1, CountExtensionItems(*builder.BuildMenu()));

  // Create more page action items to test top level menu item limitations.
  // We start at 1, so this should try to add the limit + 1.
  for (int i = 0; i < api::context_menus::ACTION_MENU_TOP_LEVEL_LIMIT; ++i)
    builder.AddContextItem(MenuItem::PAGE_ACTION);

  // We shouldn't go above the limit of top-level items.
  EXPECT_EQ(api::context_menus::ACTION_MENU_TOP_LEVEL_LIMIT,
            CountExtensionItems(*builder.BuildMenu()));
}

// Test that the "show" and "hide" menu items appear correctly in the extension
// context menu without the toolbar redesign.
TEST_F(ExtensionContextMenuModelTest, ExtensionContextMenuShowAndHideLegacy) {
  // Start with the toolbar redesign disabled.
  scoped_ptr<FeatureSwitch::ScopedOverride> toolbar_redesign_override(
      new FeatureSwitch::ScopedOverride(
          FeatureSwitch::extension_action_redesign(), false));

  InitializeEmptyExtensionService();
  Browser* browser = GetBrowser();
  extension_action_test_util::CreateToolbarModelForProfile(profile());
  scoped_refptr<const Extension> page_action =
      BuildExtension("page_action_extension",
                     manifest_keys::kPageAction,
                     Manifest::INTERNAL);
  ASSERT_TRUE(page_action.get());
  scoped_refptr<const Extension> browser_action =
      BuildExtension("browser_action_extension",
                     manifest_keys::kBrowserAction,
                     Manifest::INTERNAL);
  ASSERT_TRUE(browser_action.get());

  service()->AddExtension(page_action.get());
  service()->AddExtension(browser_action.get());

  // For laziness.
  const ExtensionContextMenuModel::MenuEntries visibility_command =
      ExtensionContextMenuModel::TOGGLE_VISIBILITY;
  base::string16 hide_string =
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_HIDE_BUTTON);

  {
    ExtensionContextMenuModel menu(page_action.get(), browser,
                                   ExtensionContextMenuModel::VISIBLE, nullptr);

    int index = GetCommandIndex(menu, visibility_command);
    // Without the toolbar redesign switch, page action menus shouldn't have a
    // visibility option.
    EXPECT_EQ(-1, index);
  }

  {
    ExtensionContextMenuModel menu(browser_action.get(), browser,
                                   ExtensionContextMenuModel::VISIBLE, nullptr);
    int index = GetCommandIndex(menu, visibility_command);
    // Browser actions should have the visibility option.
    EXPECT_NE(-1, index);
    // Since the action is currently visible, it should have the option to hide
    // it.
    EXPECT_EQ(hide_string, menu.GetLabelAt(index));
  }

  {
    ExtensionContextMenuModel menu(browser_action.get(), browser,
                                   ExtensionContextMenuModel::OVERFLOWED,
                                   nullptr);
    int index = GetCommandIndex(menu, visibility_command);
    EXPECT_NE(-1, index);
    // Without the redesign, 'hiding' refers to removing the action from the
    // toolbar entirely, so even with the action overflowed, the string should
    // be 'Hide action'.
    EXPECT_EQ(hide_string, menu.GetLabelAt(index));

    ExtensionActionAPI* action_api = ExtensionActionAPI::Get(profile());
    // At the start, the action should be visible.
    EXPECT_TRUE(action_api->GetBrowserActionVisibility(browser_action->id()));
    menu.ExecuteCommand(visibility_command, 0);
    // After execution, it should be hidden.
    EXPECT_FALSE(action_api->GetBrowserActionVisibility(browser_action->id()));

    // Cleanup - make the action visible again.
    action_api->SetBrowserActionVisibility(browser_action->id(), true);
  }
}

// Test that the "show" and "hide" menu items appear correctly in the extension
// context menu with the toolbar redesign.
TEST_F(ExtensionContextMenuModelTest, ExtensionContextMenuShowAndHideRedesign) {
  // Start with the toolbar redesign disabled.
  scoped_ptr<FeatureSwitch::ScopedOverride> toolbar_redesign_override(
      new FeatureSwitch::ScopedOverride(
          FeatureSwitch::extension_action_redesign(), true));

  InitializeEmptyExtensionService();
  Browser* browser = GetBrowser();
  extension_action_test_util::CreateToolbarModelForProfile(profile());
  scoped_refptr<const Extension> page_action =
      BuildExtension("page_action_extension",
                     manifest_keys::kPageAction,
                     Manifest::INTERNAL);
  ASSERT_TRUE(page_action.get());
  scoped_refptr<const Extension> browser_action =
      BuildExtension("browser_action_extension",
                     manifest_keys::kBrowserAction,
                     Manifest::INTERNAL);
  ASSERT_TRUE(browser_action.get());

  service()->AddExtension(page_action.get());
  service()->AddExtension(browser_action.get());

  // For laziness.
  const ExtensionContextMenuModel::MenuEntries visibility_command =
      ExtensionContextMenuModel::TOGGLE_VISIBILITY;
  base::string16 redesign_hide_string =
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_HIDE_BUTTON_IN_MENU);
  base::string16 redesign_show_string =
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_SHOW_BUTTON_IN_TOOLBAR);
  base::string16 redesign_keep_string =
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_KEEP_BUTTON_IN_TOOLBAR);

  {
    // Even page actions should have a visibility option with the redesign on.
    ExtensionContextMenuModel menu(page_action.get(), browser,
                                   ExtensionContextMenuModel::VISIBLE, nullptr);
    int index = GetCommandIndex(menu, visibility_command);
    EXPECT_NE(-1, index);
    EXPECT_EQ(redesign_hide_string, menu.GetLabelAt(index));
  }

  {
    ExtensionContextMenuModel menu(browser_action.get(), browser,
                                   ExtensionContextMenuModel::VISIBLE, nullptr);
    int index = GetCommandIndex(menu, visibility_command);
    EXPECT_NE(-1, index);
    EXPECT_EQ(redesign_hide_string, menu.GetLabelAt(index));

    ExtensionActionAPI* action_api = ExtensionActionAPI::Get(profile());
    EXPECT_TRUE(action_api->GetBrowserActionVisibility(browser_action->id()));
    // Executing the 'hide' command shouldn't modify the prefs with the redesign
    // turned on (the ordering behavior is tested in ToolbarActionsModel tests).
    menu.ExecuteCommand(visibility_command, 0);
    EXPECT_TRUE(action_api->GetBrowserActionVisibility(browser_action->id()));
  }

  {
    // If the action is overflowed, it should have the "Show button in toolbar"
    // string.
    ExtensionContextMenuModel menu(browser_action.get(), browser,
                                   ExtensionContextMenuModel::OVERFLOWED,
                                   nullptr);
    int index = GetCommandIndex(menu, visibility_command);
    EXPECT_NE(-1, index);
    EXPECT_EQ(redesign_show_string, menu.GetLabelAt(index));
  }

  {
    // If the action is transitively visible, as happens when it is showing a
    // popup, we should use a "Keep button in toolbar" string.
    ExtensionContextMenuModel menu(
        browser_action.get(), browser,
        ExtensionContextMenuModel::TRANSITIVELY_VISIBLE, nullptr);
    int index = GetCommandIndex(menu, visibility_command);
    EXPECT_NE(-1, index);
    EXPECT_EQ(redesign_keep_string, menu.GetLabelAt(index));
  }
}

TEST_F(ExtensionContextMenuModelTest, ExtensionContextUninstall) {
  InitializeEmptyExtensionService();

  scoped_refptr<const Extension> extension = BuildExtension(
      "extension", manifest_keys::kBrowserAction, Manifest::INTERNAL);
  ASSERT_TRUE(extension.get());
  service()->AddExtension(extension.get());
  const std::string extension_id = extension->id();
  ASSERT_TRUE(registry()->enabled_extensions().GetByID(extension_id));

  ScopedTestDialogAutoConfirm auto_confirm(ScopedTestDialogAutoConfirm::ACCEPT);
  TestExtensionRegistryObserver uninstalled_observer(registry());
  {
    // Scope the menu so that it's destroyed during the uninstall process. This
    // reflects what normally happens (Chrome closes the menu when the uninstall
    // dialog shows up).
    ExtensionContextMenuModel menu(extension.get(), GetBrowser(),
                                   ExtensionContextMenuModel::VISIBLE, nullptr);
    menu.ExecuteCommand(ExtensionContextMenuModel::UNINSTALL, 0);
  }
  uninstalled_observer.WaitForExtensionUninstalled();
  EXPECT_FALSE(registry()->GetExtensionById(extension_id,
                                            ExtensionRegistry::EVERYTHING));
}

}  // namespace extensions
