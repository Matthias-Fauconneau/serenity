#pragma once
#include "layout.h"

/// Implements selection of active widget/item for a \a Layout
struct Selection : virtual Layout {
    /// User changed active index.
    function<void(uint index)> activeChanged;
    /// Active index
    size_t index = -1;
    /// Set active index and emit activeChanged
    void setActive(uint index);

    bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus) override;
    bool keyPress(Key key, Modifiers modifiers) override;
};

/// Displays a selection using a blue highlight
struct HighlightSelection : virtual Selection {
    shared<Graphics> graphics(vec2 size, Rect clip) override;
};

/// Array with Selection
generic struct ArraySelection : WidgetArray<T>, virtual Selection {
	ArraySelection(){}
	ArraySelection(buffer<T>&& items) : WidgetArray<T>(move(items)){}
    /// Return active item (last selection)
    T& active() { return array<T>::at(this->index); }
    /// Clears array and resets index
	void clear() { WidgetArray<T>::clear(); index=-1; }
};

/// Vertical layout of selectable items. \sa ArraySelection
generic struct List : Vertical, ArraySelection<T>, HighlightSelection {
	List(){}
    List(array<T>&& items) : ArraySelection<T>(move(items)){}
};
