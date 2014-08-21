#pragma once

/// TextInput is an editable \a Text
struct TextInput : Text {
    /// User edited this text
    signal<const string&> textChanged;
    /// User pressed enter
    signal<const string&> textEntered;
    /// Cursor start position for selections
    Cursor selectionStart;

    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
    bool keyPress(Key key, Modifiers modifiers) override;
    void render() override;
};
