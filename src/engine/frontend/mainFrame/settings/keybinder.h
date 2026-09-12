#ifndef KEY_BINDER_H
#define KEY_BINDER_H

#include <wx/accel.h>
#include <vector>

#include "frontend/frontend.h"

//
// Forward declarations.
//

class wxWindow;
class wxMenuBar;
class wxXmlNode;

/**
 * Binds keys to commands.
 */
class FRONTEND_API ibKeyBinder
{

public:

    struct Key
    {
        int                 flags;
        int                 code;
    };

    struct Command
    {
        int                 id;
        wxString            name;
        wxString            group;
        wxString            help;
        std::vector<Key>    keys;
        // What the code binds it to (SetShortcut). Kept beside `keys` so the saved profile holds only what
        // the person CHANGED - see Save.
        std::vector<Key>    defaultKeys;
    };

    /**
     * Constructor.
     */
    ibKeyBinder();

    /**
     * Destructor.
     */
    virtual ~ibKeyBinder();

    /**
     * Removes all of the commands from the key binder.
     */
    void ClearCommands();

    /**
     * Finds the command that is bound to the specified id. If there is no such
     * command the method returns nullptr.
     */
    Command* GetCommandForId(int id) const;

    /**
     * Adds a command that can be hot keyed.
     */
    void AddCommand(const Command& command);

    /**
     * Adds a command that can be hot keyed.
     */
    void AddCommand(int id, const wxString& group, const wxString& name, const wxString& help);

    /**
     * Adds a command for the specified menu item.
     */
    void AddCommand(const wxString& group, wxMenuItem* menuItem);

    /**
     * Adds all of the commands on a menu bar to the key binder.
     */
    void AddCommandsFromMenuBar(wxMenuBar* menu);

    /**
     * Adds all of the commands on a menu to the key binder.
     */
    void AddCommandsFromMenu(const wxString& group, wxMenu* menu);

    /**
     * Removes the command with the specified id.
     */
    void RemoveCommand(int id);

    /**
     * Updates the accelerator for a window with the key bindings.
     */
    void UpdateWindow(wxWindow* window);

    /**
     * Updates the text on all of the menus in the menu bar to reflect the
     * current key bindings.
     */
    void UpdateMenuBar(wxMenuBar* menuBar);

    /**
     * Updates the text on the menu item to reflect the current key binding.
     * If a name is supplied it's used as the label on the menu, otherwise
     * the corresponding command name is used.
     */
    bool GetMenuItemText(wxMenuItem* item, wxString& label) const;

    /**
     * Sets the DEFAULT shortcut for the specified id - the binding the code
     * gives the command, which a loaded profile may then change.
     */
    void SetShortcut(int id, int flags, int key);

    /**
     * Sets the default shortcut for the specified id. The shortcut should have
     * a form like "Ctrl+F3".
     */
    void SetShortcut(int id, const wxString& shortcut);

    /**
     * Saves the key bindings in XML format. The tag is the name that is given
     * to the root node.
     *
     * Only the commands whose keys differ from their defaults are written. A
     * command is saved under its numeric id, and ids move whenever a command is
     * added before another one; a profile that held EVERY binding carried the
     * old numbering forward for good - F10 landed on "Attach for debugging"
     * after two start commands were added ahead of the steps (2026-09-11). The
     * defaults now always come from the code; the profile holds what a person
     * chose, and nothing else.
     */
    wxXmlNode* Save(const wxString& tag) const;

    /**
     * Loads the key bindings from XML format, over the defaults: a command in
     * the profile takes the keys saved for it (none, if the person removed
     * them), every other command keeps its default.
     */
    void Load(wxXmlNode* root);

    /**
     * Returns the number of commands managed by the key binder.
     */
    unsigned int GetNumCommands() const;

    /**
     * Returns the ith command.
     */
    const Command& GetCommand(unsigned int i) const;

    /**
     * Returns the human readable string representing the key binding.
     */
    static wxString GetKeyBindingAsText(const Key& key);

    /**
     * Parses a human readable string into a key binding.
     */
    static Key GetTextAsKeyBinding(const wxString& text);

private:

    /**
     * Updates the text on the menu to reflect the current key bindings.
     */
    void UpdateMenu(wxMenu* menu);

    /**
     */
    static int StringToKeyCode(const wxString &keyName);

    /**
     * Loads a command key binding in XML format. The root node passed in
     * should be a"command" node.
     */
    void LoadCommand(wxXmlNode* root);

private:

    std::vector<Command*>   m_commands;
    wxAcceleratorTable      m_accel;

};

#endif