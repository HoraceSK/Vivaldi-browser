// Copyright (c) 2019 Vivaldi Technologies AS. All rights reserved.

#include "chrome/browser/importer/profile_writer.h"

#include "app/vivaldi_resources.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/vivaldi_bookmark_kit.h"
#include "importer/imported_notes_entry.h"
#include "importer/imported_speeddial_entry.h"
#include "notes/notesnode.h"
#include "notes/notes_factory.h"
#include "notes/notes_model.h"
#include "ui/base/l10n/l10n_util.h"

using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;
using vivaldi::Notes_Model;
using vivaldi::Notes_Node;
using vivaldi::NotesModelFactory;

namespace {

// Generates a unique folder name. If |folder_name| is not unique, then this
// repeatedly tests for '|folder_name| + (i)' until a unique name is found.
base::string16 VivaldiGenerateUniqueFolderName(BookmarkModel* model,
                                        const base::string16& folder_name) {
  // Build a set containing the bookmark bar folder names.
  std::set<base::string16> existing_folder_names;
  const BookmarkNode* bookmark_bar = model->bookmark_bar_node();
  for (const auto& node : bookmark_bar->children()) {
    if (node->is_folder())
      existing_folder_names.insert(node->GetTitle());
  }

  // If the given name is unique, use it.
  if (existing_folder_names.find(folder_name) == existing_folder_names.end())
    return folder_name;

  // Otherwise iterate until we find a unique name.
  for (size_t i = 1; i <= existing_folder_names.size(); ++i) {
    base::string16 name = folder_name + base::ASCIIToUTF16(" (") +
                          base::NumberToString16(i) + base::ASCIIToUTF16(")");
    if (existing_folder_names.find(name) == existing_folder_names.end())
      return name;
  }

  NOTREACHED();
  return folder_name;
}

}  // namespace

void ProfileWriter::AddSpeedDial(
    const std::vector<ImportedSpeedDialEntry>& speeddial) {
  if (speeddial.empty())
    return;

  BookmarkModel* model = BookmarkModelFactory::GetForBrowserContext(profile_);
  DCHECK(model->loaded());
  const BookmarkNode* bookmark_bar = model->bookmark_bar_node();
  const base::string16& first_folder_name =
      l10n_util::GetStringUTF16(IDS_SPEEDDIAL_GROUP_FROM_OPERA);

  model->BeginExtensiveChanges();

  const BookmarkNode* top_level_folder = NULL;

  base::string16 name = VivaldiGenerateUniqueFolderName(model, first_folder_name);

  vivaldi_bookmark_kit::CustomMetaInfo vivaldi_meta;
  vivaldi_meta.SetSpeeddial(true);
  top_level_folder = model->AddFolder(
      bookmark_bar, bookmark_bar->children().size(), name, vivaldi_meta.map());

  for (auto& item : speeddial) {
    if (!model->AddURL(top_level_folder, top_level_folder->children().size(),
                       item.title, item.url))
      break;
  }

  model->EndExtensiveChanges();
}

void ProfileWriter::AddNotes(const std::vector<ImportedNotesEntry>& notes,
                             const base::string16& top_level_folder_name) {
  Notes_Model* model = NotesModelFactory::GetForBrowserContext(profile_);

  model->BeginExtensiveChanges();

  std::set<const Notes_Node*> folders_added_to;
  const Notes_Node* top_level_folder = NULL;
  for (std::vector<ImportedNotesEntry>::const_iterator note = notes.begin();
       note != notes.end(); ++note) {
    const Notes_Node* parent = NULL;
    // Add to a folder that will contain all the imported notes.
    // The first time we do so, create the folder.
    if (!top_level_folder) {
      base::string16 name;
      name = l10n_util::GetStringUTF16(IDS_NOTES_GROUP_FROM_OPERA);
      top_level_folder = model->AddFolder(
          model->main_node(), model->main_node()->children().size(), name);
    }
    parent = top_level_folder;

    // Ensure any enclosing folders are present in the model.  The note's
    // enclosing folder structure should be
    //   path[0] > path[1] > ... > path[size() - 1]
    for (std::vector<base::string16>::const_iterator folder_name =
             note->path.begin();
         folder_name != note->path.end(); ++folder_name) {
      const Notes_Node* child = NULL;
      for (size_t index = 0; index < parent->children().size(); ++index) {
        const Notes_Node* node = parent->children()[index].get();
        if (node->is_folder() && node->GetTitle() == *folder_name) {
          child = node;
          break;
        }
      }
      if (!child) {
        child =
            model->AddFolder(parent, parent->children().size(), *folder_name);
      }
      parent = child;
    }

    folders_added_to.insert(parent);
    model->AddNote(parent, parent->children().size(), note->is_folder, *note);
  }
  model->EndExtensiveChanges();
}