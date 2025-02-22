// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/bookmarks/bookmark_menu_delegate.h"

#include "base/prefs/pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/managed_bookmark_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/bookmarks/bookmark_drag_drop.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils_desktop.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"
#include "chrome/browser/ui/views/event_utils.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/bookmarks/managed/managed_bookmark_service.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/user_metrics.h"
#include "grit/theme_resources.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/window_open_disposition.h"
#include "ui/resources/grit/ui_resources.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/widget/widget.h"

using base::UserMetricsAction;
using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;
using bookmarks::BookmarkNodeData;
using content::PageNavigator;
using views::MenuItemView;

// Max width of a menu. There does not appear to be an OS value for this, yet
// both IE and FF restrict the max width of a menu.
static const int kMaxMenuWidth = 400;

BookmarkMenuDelegate::BookmarkMenuDelegate(Browser* browser,
                                           PageNavigator* navigator,
                                           views::Widget* parent)
    : browser_(browser),
      profile_(browser->profile()),
      page_navigator_(navigator),
      parent_(parent),
      menu_(NULL),
      parent_menu_item_(NULL),
      next_menu_id_(IDC_FIRST_BOOKMARK_MENU),
      real_delegate_(NULL),
      is_mutating_model_(false),
      location_(BOOKMARK_LAUNCH_LOCATION_NONE) {
}

BookmarkMenuDelegate::~BookmarkMenuDelegate() {
  GetBookmarkModel()->RemoveObserver(this);
}

void BookmarkMenuDelegate::Init(views::MenuDelegate* real_delegate,
                                MenuItemView* parent,
                                const BookmarkNode* node,
                                int start_child_index,
                                ShowOptions show_options,
                                BookmarkLaunchLocation location) {
  GetBookmarkModel()->AddObserver(this);
  real_delegate_ = real_delegate;
  location_ = location;
  if (parent) {
    parent_menu_item_ = parent;

    // Add a separator if there are existing items in the menu, and if the
    // current node has children. If |node| is the bookmark bar then the
    // managed node is shown as its first child, if it's not empty.
    BookmarkModel* model = GetBookmarkModel();
    bookmarks::ManagedBookmarkService* managed = GetManagedBookmarkService();
    bool show_forced_folders = show_options == SHOW_PERMANENT_FOLDERS &&
                               node == model->bookmark_bar_node();
    bool show_managed =
        show_forced_folders && !managed->managed_node()->empty();
    bool show_supervised =
        show_forced_folders && !managed->supervised_node()->empty();
    bool has_children = (start_child_index < node->child_count()) ||
                        show_managed || show_supervised;
    int initial_count = parent->GetSubmenu() ?
        parent->GetSubmenu()->GetMenuItemCount() : 0;
    if (has_children && initial_count > 0)
      parent->AppendSeparator();
    if (show_managed)
      BuildMenuForManagedNode(parent);
    if (show_supervised)
      BuildMenuForSupervisedNode(parent);
    BuildMenu(node, start_child_index, parent);
    if (show_options == SHOW_PERMANENT_FOLDERS)
      BuildMenusForPermanentNodes(parent);
  } else {
    menu_ = CreateMenu(node, start_child_index, show_options);
  }
}

void BookmarkMenuDelegate::SetPageNavigator(PageNavigator* navigator) {
  page_navigator_ = navigator;
  if (context_menu_.get())
    context_menu_->SetPageNavigator(navigator);
}

BookmarkModel* BookmarkMenuDelegate::GetBookmarkModel() {
  return BookmarkModelFactory::GetForProfile(profile_);
}

bookmarks::ManagedBookmarkService*
BookmarkMenuDelegate::GetManagedBookmarkService() {
  return ManagedBookmarkServiceFactory::GetForProfile(profile_);
}

void BookmarkMenuDelegate::SetActiveMenu(const BookmarkNode* node,
                                         int start_index) {
  DCHECK(!parent_menu_item_);
  if (!node_to_menu_map_[node])
    CreateMenu(node, start_index, HIDE_PERMANENT_FOLDERS);
  menu_ = node_to_menu_map_[node];
}

base::string16 BookmarkMenuDelegate::GetTooltipText(
    int id,
    const gfx::Point& screen_loc) const {
  MenuIDToNodeMap::const_iterator i = menu_id_to_node_map_.find(id);
  // When removing bookmarks it may be possible to end up here without a node.
  if (i == menu_id_to_node_map_.end()) {
    DCHECK(is_mutating_model_);
    return base::string16();
  }

  const BookmarkNode* node = i->second;
  if (node->is_url()) {
    return BookmarkBarView::CreateToolTipForURLAndTitle(
        parent_, screen_loc, node->url(), node->GetTitle(), profile_);
  }
  return base::string16();
}

bool BookmarkMenuDelegate::IsTriggerableEvent(views::MenuItemView* menu,
                                              const ui::Event& e) {
  return e.type() == ui::ET_GESTURE_TAP ||
         e.type() == ui::ET_GESTURE_TAP_DOWN ||
         event_utils::IsPossibleDispositionEvent(e);
}

void BookmarkMenuDelegate::ExecuteCommand(int id, int mouse_event_flags) {
  DCHECK(menu_id_to_node_map_.find(id) != menu_id_to_node_map_.end());

  const BookmarkNode* node = menu_id_to_node_map_[id];
  std::vector<const BookmarkNode*> selection;
  selection.push_back(node);

  chrome::OpenAll(parent_->GetNativeWindow(), page_navigator_, selection,
                  ui::DispositionFromEventFlags(mouse_event_flags),
                  profile_);
  RecordBookmarkLaunch(node, location_);
}

bool BookmarkMenuDelegate::ShouldExecuteCommandWithoutClosingMenu(
    int id,
    const ui::Event& event) {
  return (event.flags() & ui::EF_LEFT_MOUSE_BUTTON) &&
         ui::DispositionFromEventFlags(event.flags()) == NEW_BACKGROUND_TAB;
}

bool BookmarkMenuDelegate::GetDropFormats(
    MenuItemView* menu,
    int* formats,
    std::set<ui::Clipboard::FormatType>* format_types) {
  *formats = ui::OSExchangeData::URL;
  format_types->insert(BookmarkNodeData::GetBookmarkFormatType());
  return true;
}

bool BookmarkMenuDelegate::AreDropTypesRequired(MenuItemView* menu) {
  return true;
}

bool BookmarkMenuDelegate::CanDrop(MenuItemView* menu,
                                   const ui::OSExchangeData& data) {
  // Only accept drops of 1 node, which is the case for all data dragged from
  // bookmark bar and menus.

  if (!drop_data_.Read(data) || drop_data_.size() != 1 ||
      !profile_->GetPrefs()->GetBoolean(
          bookmarks::prefs::kEditBookmarksEnabled))
    return false;

  if (drop_data_.has_single_url())
    return true;

  const BookmarkNode* drag_node =
      drop_data_.GetFirstNode(GetBookmarkModel(), profile_->GetPath());
  if (!drag_node) {
    // Dragging a folder from another profile, always accept.
    return true;
  }

  // Drag originated from same profile and is not a URL. Only accept it if
  // the dragged node is not a parent of the node menu represents.
  if (menu_id_to_node_map_.find(menu->GetCommand()) ==
      menu_id_to_node_map_.end()) {
    // If we don't know the menu assume its because we're embedded. We'll
    // figure out the real operation when GetDropOperation is invoked.
    return true;
  }
  const BookmarkNode* drop_node = menu_id_to_node_map_[menu->GetCommand()];
  DCHECK(drop_node);
  while (drop_node && drop_node != drag_node)
    drop_node = drop_node->parent();
  return (drop_node == NULL);
}

int BookmarkMenuDelegate::GetDropOperation(
    MenuItemView* item,
    const ui::DropTargetEvent& event,
    views::MenuDelegate::DropPosition* position) {
  // Should only get here if we have drop data.
  DCHECK(drop_data_.is_valid());

  const BookmarkNode* node = menu_id_to_node_map_[item->GetCommand()];
  const BookmarkNode* drop_parent = node->parent();
  int index_to_drop_at = drop_parent->GetIndexOf(node);
  BookmarkModel* model = GetBookmarkModel();
  switch (*position) {
    case views::MenuDelegate::DROP_AFTER:
      if (node == model->other_node() || node == model->mobile_node()) {
        // Dropping after these nodes makes no sense.
        *position = views::MenuDelegate::DROP_NONE;
      }
      index_to_drop_at++;
      break;

    case views::MenuDelegate::DROP_BEFORE:
      if (node == model->mobile_node()) {
        // Dropping before this node makes no sense.
        *position = views::MenuDelegate::DROP_NONE;
      }
      break;

    case views::MenuDelegate::DROP_ON:
      drop_parent = node;
      index_to_drop_at = node->child_count();
      break;

    default:
      break;
  }
  DCHECK(drop_parent);
  return chrome::GetBookmarkDropOperation(
      profile_, event, drop_data_, drop_parent, index_to_drop_at);
}

int BookmarkMenuDelegate::OnPerformDrop(
    MenuItemView* menu,
    views::MenuDelegate::DropPosition position,
    const ui::DropTargetEvent& event) {
  const BookmarkNode* drop_node = menu_id_to_node_map_[menu->GetCommand()];
  DCHECK(drop_node);
  BookmarkModel* model = GetBookmarkModel();
  DCHECK(model);
  const BookmarkNode* drop_parent = drop_node->parent();
  DCHECK(drop_parent);
  int index_to_drop_at = drop_parent->GetIndexOf(drop_node);
  switch (position) {
    case views::MenuDelegate::DROP_AFTER:
      index_to_drop_at++;
      break;

    case views::MenuDelegate::DROP_ON:
      DCHECK(drop_node->is_folder());
      drop_parent = drop_node;
      index_to_drop_at = drop_node->child_count();
      break;

    case views::MenuDelegate::DROP_BEFORE:
      if (drop_node == model->other_node() ||
          drop_node == model->mobile_node()) {
        // This can happen with SHOW_PERMANENT_FOLDERS.
        drop_parent = model->bookmark_bar_node();
        index_to_drop_at = drop_parent->child_count();
      }
      break;

    default:
      break;
  }

  bool copy = event.source_operations() == ui::DragDropTypes::DRAG_COPY;
  return chrome::DropBookmarks(profile_, drop_data_,
                               drop_parent, index_to_drop_at, copy);
}

bool BookmarkMenuDelegate::ShowContextMenu(MenuItemView* source,
                                           int id,
                                           const gfx::Point& p,
                                           ui::MenuSourceType source_type) {
  DCHECK(menu_id_to_node_map_.find(id) != menu_id_to_node_map_.end());
  std::vector<const BookmarkNode*> nodes;
  nodes.push_back(menu_id_to_node_map_[id]);
  bool close_on_delete = !parent_menu_item_ &&
      (nodes[0]->parent() == GetBookmarkModel()->other_node() &&
       nodes[0]->parent()->child_count() == 1);
  context_menu_.reset(
      new BookmarkContextMenu(
          parent_,
          browser_,
          profile_,
          page_navigator_,
          nodes[0]->parent(),
          nodes,
          close_on_delete));
  context_menu_->set_observer(this);
  context_menu_->RunMenuAt(p, source_type);
  context_menu_.reset(NULL);
  return true;
}

bool BookmarkMenuDelegate::CanDrag(MenuItemView* menu) {
  const BookmarkNode* node = menu_id_to_node_map_[menu->GetCommand()];
  // Don't let users drag the other folder.
  return node->parent() != GetBookmarkModel()->root_node();
}

void BookmarkMenuDelegate::WriteDragData(MenuItemView* sender,
                                         ui::OSExchangeData* data) {
  DCHECK(sender && data);

  content::RecordAction(UserMetricsAction("BookmarkBar_DragFromFolder"));

  BookmarkNodeData drag_data(menu_id_to_node_map_[sender->GetCommand()]);
  drag_data.Write(profile_->GetPath(), data);
}

int BookmarkMenuDelegate::GetDragOperations(MenuItemView* sender) {
  return chrome::GetBookmarkDragOperation(
      profile_, menu_id_to_node_map_[sender->GetCommand()]);
}

int BookmarkMenuDelegate::GetMaxWidthForMenu(MenuItemView* menu) {
  return kMaxMenuWidth;
}

void BookmarkMenuDelegate::WillShowMenu(MenuItemView* menu) {
  auto iter = menu_id_to_node_map_.find(menu->GetCommand());
  if ((iter != menu_id_to_node_map_.end()) && iter->second->child_count() &&
      !menu->GetSubmenu()->GetMenuItemCount())
    BuildMenu(iter->second, 0, menu);
}

void BookmarkMenuDelegate::BookmarkModelChanged() {
}

void BookmarkMenuDelegate::BookmarkNodeFaviconChanged(
    BookmarkModel* model,
    const BookmarkNode* node) {
  NodeToMenuMap::iterator menu_pair = node_to_menu_map_.find(node);
  if (menu_pair == node_to_menu_map_.end())
    return;  // We're not showing a menu item for the node.

  menu_pair->second->SetIcon(model->GetFavicon(node).AsImageSkia());
}

void BookmarkMenuDelegate::WillRemoveBookmarks(
    const std::vector<const BookmarkNode*>& bookmarks) {
  DCHECK(!is_mutating_model_);
  is_mutating_model_ = true;  // Set to false in DidRemoveBookmarks().

  // Remove the observer so that when the remove happens we don't prematurely
  // cancel the menu. The observer is added back in DidRemoveBookmarks().
  GetBookmarkModel()->RemoveObserver(this);

  // Remove the menu items.
  std::set<MenuItemView*> changed_parent_menus;
  for (std::vector<const BookmarkNode*>::const_iterator i(bookmarks.begin());
       i != bookmarks.end(); ++i) {
    NodeToMenuMap::iterator node_to_menu = node_to_menu_map_.find(*i);
    if (node_to_menu != node_to_menu_map_.end()) {
      MenuItemView* menu = node_to_menu->second;
      MenuItemView* parent = menu->GetParentMenuItem();
      // |parent| is NULL when removing a root. This happens when right clicking
      // to delete an empty folder.
      if (parent) {
        changed_parent_menus.insert(parent);
        parent->RemoveMenuItemAt(menu->parent()->GetIndexOf(menu));
      }
      node_to_menu_map_.erase(node_to_menu);
      menu_id_to_node_map_.erase(menu->GetCommand());
    }
  }

  // All the bookmarks in |bookmarks| should have the same parent. It's possible
  // to support different parents, but this would need to prune any nodes whose
  // parent has been removed. As all nodes currently have the same parent, there
  // is the DCHECK.
  DCHECK_LE(changed_parent_menus.size(), 1U);

  // Remove any descendants of the removed nodes in |node_to_menu_map_|.
  for (NodeToMenuMap::iterator i(node_to_menu_map_.begin());
       i != node_to_menu_map_.end(); ) {
    bool ancestor_removed = false;
    for (std::vector<const BookmarkNode*>::const_iterator j(bookmarks.begin());
         j != bookmarks.end(); ++j) {
      if (i->first->HasAncestor(*j)) {
        ancestor_removed = true;
        break;
      }
    }
    if (ancestor_removed) {
      menu_id_to_node_map_.erase(i->second->GetCommand());
      node_to_menu_map_.erase(i++);
    } else {
      ++i;
    }
  }

  for (std::set<MenuItemView*>::const_iterator i(changed_parent_menus.begin());
       i != changed_parent_menus.end(); ++i)
    (*i)->ChildrenChanged();
}

void BookmarkMenuDelegate::DidRemoveBookmarks() {
  // Balances remove in WillRemoveBookmarksImpl.
  GetBookmarkModel()->AddObserver(this);
  DCHECK(is_mutating_model_);
  is_mutating_model_ = false;
}

MenuItemView* BookmarkMenuDelegate::CreateMenu(const BookmarkNode* parent,
                                               int start_child_index,
                                               ShowOptions show_options) {
  MenuItemView* menu = new MenuItemView(real_delegate_);
  menu->SetCommand(next_menu_id_++);
  AddMenuToMaps(menu, parent);
  menu->set_has_icons(true);
  bool show_permanent = show_options == SHOW_PERMANENT_FOLDERS;
  if (show_permanent && parent == GetBookmarkModel()->bookmark_bar_node()) {
    BuildMenuForManagedNode(menu);
    BuildMenuForSupervisedNode(menu);
  }
  BuildMenu(parent, start_child_index, menu);
  if (show_permanent)
    BuildMenusForPermanentNodes(menu);
  return menu;
}

void BookmarkMenuDelegate::BuildMenusForPermanentNodes(
    views::MenuItemView* menu) {
  BookmarkModel* model = GetBookmarkModel();
  bool added_separator = false;
  BuildMenuForPermanentNode(model->other_node(),
                            chrome::GetBookmarkFolderIcon(), menu,
                            &added_separator);
  BuildMenuForPermanentNode(model->mobile_node(),
                            chrome::GetBookmarkFolderIcon(), menu,
                            &added_separator);
}

void BookmarkMenuDelegate::BuildMenuForPermanentNode(const BookmarkNode* node,
                                                     const gfx::ImageSkia& icon,
                                                     MenuItemView* menu,
                                                     bool* added_separator) {
  if (!node->IsVisible() || node->GetTotalNodeCount() == 1)
    return;  // No children, don't create a menu.

  if (!*added_separator) {
    *added_separator = true;
    menu->AppendSeparator();
  }

  AddMenuToMaps(
      menu->AppendSubMenuWithIcon(next_menu_id_++, node->GetTitle(), icon),
      node);
}

void BookmarkMenuDelegate::BuildMenuForManagedNode(MenuItemView* menu) {
  // Don't add a separator for this menu.
  bool added_separator = true;
  const BookmarkNode* node = GetManagedBookmarkService()->managed_node();
  BuildMenuForPermanentNode(node, chrome::GetBookmarkManagedFolderIcon(), menu,
                            &added_separator);
}

void BookmarkMenuDelegate::BuildMenuForSupervisedNode(MenuItemView* menu) {
  // Don't add a separator for this menu.
  bool added_separator = true;
  const BookmarkNode* node = GetManagedBookmarkService()->supervised_node();
  BuildMenuForPermanentNode(node, chrome::GetBookmarkSupervisedFolderIcon(),
                            menu, &added_separator);
}

void BookmarkMenuDelegate::BuildMenu(const BookmarkNode* parent,
                                     int start_child_index,
                                     MenuItemView* menu) {
  DCHECK(parent->empty() || start_child_index < parent->child_count());
  ui::ResourceBundle* rb = &ui::ResourceBundle::GetSharedInstance();
  for (int i = start_child_index; i < parent->child_count(); ++i) {
    const BookmarkNode* node = parent->GetChild(i);
    const int id = next_menu_id_++;
    MenuItemView* child_menu_item;
    if (node->is_url()) {
      const gfx::Image& image = GetBookmarkModel()->GetFavicon(node);
      const gfx::ImageSkia* icon = image.IsEmpty() ?
          rb->GetImageSkiaNamed(IDR_DEFAULT_FAVICON) : image.ToImageSkia();
      child_menu_item =
          menu->AppendMenuItemWithIcon(id, node->GetTitle(), *icon);
    } else {
      DCHECK(node->is_folder());
      child_menu_item = menu->AppendSubMenuWithIcon(
          id, node->GetTitle(), chrome::GetBookmarkFolderIcon());
    }
    AddMenuToMaps(child_menu_item, node);
  }
}

void BookmarkMenuDelegate::AddMenuToMaps(MenuItemView* menu,
                                         const BookmarkNode* node) {
  menu_id_to_node_map_[menu->GetCommand()] = node;
  node_to_menu_map_[node] = menu;
}
