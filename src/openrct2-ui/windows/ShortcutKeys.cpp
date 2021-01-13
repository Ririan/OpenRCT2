/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "../input/ShortcutManager.h"
#include "Window.h"

#include <openrct2-ui/interface/Widget.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/localisation/Localisation.h>
#include <openrct2/sprites.h>

using namespace OpenRCT2;
using namespace OpenRCT2::Ui;

static constexpr const rct_string_id WINDOW_TITLE = STR_SHORTCUTS_TITLE;
static constexpr const int32_t WW = 420;
static constexpr const int32_t WH = 280;

static constexpr const int32_t WW_SC_MAX = 1200;
static constexpr const int32_t WH_SC_MAX = 800;

enum WINDOW_SHORTCUT_WIDGET_IDX
{
    WIDX_BACKGROUND,
    WIDX_TITLE,
    WIDX_CLOSE,
    WIDX_TAB_CONTENT_PANEL,
    WIDX_SCROLL,
    WIDX_RESET,
    WIDX_TAB_0,
};

// clang-format off
static rct_widget window_shortcut_widgets[] = {
    WINDOW_SHIM(WINDOW_TITLE, WW, WH),
    MakeWidget({0,    43}, {350, 287}, WindowWidgetType::Resize, WindowColour::Secondary),
    MakeWidget({4,    47}, {412, 245}, WindowWidgetType::Scroll, WindowColour::Primary, SCROLL_VERTICAL,           STR_SHORTCUT_LIST_TIP        ),
    MakeWidget({4, WH-15}, {150,  12}, WindowWidgetType::Button, WindowColour::Primary, STR_SHORTCUT_ACTION_RESET, STR_SHORTCUT_ACTION_RESET_TIP),
    { WIDGETS_END }
};
// clang-format on

static constexpr const rct_string_id CHANGE_WINDOW_TITLE = STR_SHORTCUT_CHANGE_TITLE;
static constexpr const int32_t CHANGE_WW = 250;
static constexpr const int32_t CHANGE_WH = 60;

// clang-format off
static rct_widget window_shortcut_change_widgets[] = {
    WINDOW_SHIM(CHANGE_WINDOW_TITLE, CHANGE_WW, CHANGE_WH),
    { WIDGETS_END }
};
// clang-format on

class ChangeShortcutWindow final : public Window
{
private:
    rct_string_id _shortcutLocalisedName{};
    std::string _shortcutCustomName;

public:
    static ChangeShortcutWindow* Open(std::string_view shortcutId)
    {
        auto& shortcutManager = GetShortcutManager();
        auto registeredShortcut = shortcutManager.GetShortcut(shortcutId);
        if (registeredShortcut != nullptr)
        {
            window_close_by_class(WC_CHANGE_KEYBOARD_SHORTCUT);
            auto w = WindowCreate<ChangeShortcutWindow>(WC_CHANGE_KEYBOARD_SHORTCUT, CHANGE_WW, CHANGE_WH, WF_CENTRE_SCREEN);
            if (w != nullptr)
            {
                w->_shortcutLocalisedName = registeredShortcut->LocalisedName;
                w->_shortcutCustomName = registeredShortcut->CustomName;
                shortcutManager.SetPendingShortcutChange(registeredShortcut->Id);
                return w;
            }
        }
        return nullptr;
    }

    void OnOpen() override
    {
        widgets = window_shortcut_change_widgets;
        enabled_widgets = (1ULL << WIDX_CLOSE);
        WindowInitScrollWidgets(this);
    }

    void OnClose() override
    {
        auto& shortcutManager = GetShortcutManager();
        shortcutManager.SetPendingShortcutChange({});
        NotifyShortcutKeysWindow();
    }

    void OnMouseUp(rct_widgetindex widgetIndex) override
    {
        switch (widgetIndex)
        {
            case WIDX_CLOSE:
                Close();
                break;
        }
    }

    void OnDraw(rct_drawpixelinfo& dpi) override
    {
        DrawWidgets(dpi);

        ScreenCoordsXY stringCoords(windowPos.x + 125, windowPos.y + 30);

        auto ft = Formatter();
        if (_shortcutCustomName.empty())
        {
            ft.Add<rct_string_id>(_shortcutLocalisedName);
        }
        else
        {
            ft.Add<rct_string_id>(STR_STRING);
            ft.Add<const char*>(_shortcutCustomName.c_str());
        }
        gfx_draw_string_centred_wrapped(&dpi, ft.Data(), stringCoords, 242, STR_SHORTCUT_CHANGE_PROMPT, COLOUR_BLACK);
    }

private:
    void NotifyShortcutKeysWindow();
};

class ShortcutKeysWindow final : public Window
{
private:
    struct ShortcutStringPair
    {
        size_t ShortcutIndex{};
        std::string ShortcutId;
        rct_string_id StringId = STR_NONE;
        std::string CustomString;
        std::string Binding;
    };

    struct ShortcutTabDesc
    {
        std::string_view IdGroup;
        uint32_t ImageId;
        uint32_t ImageDivisor;
        uint32_t ImageNumFrames;
    };

    std::vector<ShortcutTabDesc> _tabs;
    std::vector<rct_widget> _widgets;
    std::vector<ShortcutStringPair> _list;
    std::optional<size_t> _highlightedItem;
    size_t _currentTabIndex{};
    uint32_t _tabAnimationIndex{};

public:
    void OnOpen() override
    {
        InitialiseTabs();
        InitialiseWidgets();
        InitialiseList();

        min_width = WW;
        min_height = WH;
        max_width = WW_SC_MAX;
        max_height = WH_SC_MAX;
    }

    void OnResize() override
    {
        window_set_resize(this, min_width, min_height, max_width, max_height);
    }

    void OnUpdate() override
    {
        _tabAnimationIndex++;
    }

    void OnMouseUp(rct_widgetindex widgetIndex) override
    {
        switch (widgetIndex)
        {
            case WIDX_CLOSE:
                Close();
                break;
            case WIDX_RESET:
                ResetAll();
                break;
            default:
            {
                auto tabIndex = static_cast<size_t>(widgetIndex - WIDX_TAB_0);
                if (tabIndex < _tabs.size())
                {
                    SetTab(tabIndex);
                }
            }
        }
    }

    void OnPrepareDraw() override
    {
        widgets[WIDX_BACKGROUND].right = width - 1;
        widgets[WIDX_BACKGROUND].bottom = height - 1;
        widgets[WIDX_TITLE].right = width - 2;
        widgets[WIDX_CLOSE].right = width - 3;
        widgets[WIDX_CLOSE].left = width - 13;
        widgets[WIDX_TAB_CONTENT_PANEL].right = width - 1;
        widgets[WIDX_TAB_CONTENT_PANEL].bottom = height - 1;
        widgets[WIDX_SCROLL].right = width - 5;
        widgets[WIDX_SCROLL].bottom = height - 19;
        widgets[WIDX_RESET].top = height - 16;
        widgets[WIDX_RESET].bottom = height - 5;
        window_align_tabs(this, WIDX_TAB_0, static_cast<rct_widgetindex>(WIDX_TAB_0 + _tabs.size()));

        // Set selected tab
        for (size_t i = 0; i < _tabs.size(); i++)
        {
            SetWidgetPressed(static_cast<rct_widgetindex>(WIDX_TAB_0 + i), false);
        }
        SetWidgetPressed(static_cast<rct_widgetindex>(WIDX_TAB_0 + _currentTabIndex), true);
    }

    void OnDraw(rct_drawpixelinfo& dpi) override
    {
        DrawWidgets(dpi);
        DrawTabImages(dpi);
    }

    ScreenSize OnScrollGetSize(int32_t scrollIndex) override
    {
        return { 0, static_cast<int32_t>(_list.size() * SCROLLABLE_ROW_HEIGHT) };
    }

    void OnScrollMouseOver(int32_t scrollIndex, const ScreenCoordsXY& screenCoords) override
    {
        auto index = static_cast<size_t>((screenCoords.y - 1) / SCROLLABLE_ROW_HEIGHT);
        if (index < _list.size())
        {
            _highlightedItem = index;
            Invalidate();
        }
    }

    void OnScrollMouseDown(int32_t scrollIndex, const ScreenCoordsXY& screenCoords) override
    {
        auto selectedItem = static_cast<size_t>((screenCoords.y - 1) / SCROLLABLE_ROW_HEIGHT);
        if (selectedItem < _list.size())
        {
            // Is this a separator?
            if (!_list[selectedItem].ShortcutId.empty())
            {
                auto& shortcut = _list[selectedItem];
                ChangeShortcutWindow::Open(shortcut.ShortcutId);
            }
        }
    }

    void OnScrollDraw(int32_t scrollIndex, rct_drawpixelinfo& dpi) override
    {
        auto dpiCoords = ScreenCoordsXY{ dpi.x, dpi.y };
        gfx_fill_rect(
            &dpi, { dpiCoords, dpiCoords + ScreenCoordsXY{ dpi.width - 1, dpi.height - 1 } }, ColourMapA[colours[1]].mid_light);

        // TODO: the line below is a workaround for what is presumably a bug with dpi->width
        //       see https://github.com/OpenRCT2/OpenRCT2/issues/11238 for details
        const auto scrollWidth = width - SCROLLBAR_WIDTH - 10;

        for (size_t i = 0; i < _list.size(); ++i)
        {
            auto y = static_cast<int32_t>(1 + i * SCROLLABLE_ROW_HEIGHT);
            if (y > dpi.y + dpi.height)
            {
                break;
            }

            if (y + SCROLLABLE_ROW_HEIGHT < dpi.y)
            {
                continue;
            }

            // Is this a separator?
            if (_list[i].ShortcutId.empty())
            {
                DrawSeparator(dpi, y, scrollWidth);
            }
            else
            {
                auto isHighlighted = _highlightedItem == i;
                DrawItem(dpi, y, scrollWidth, _list[i], isHighlighted);
            }
        }
    }

    void RefreshBindings()
    {
        InitialiseList();
    }

private:
    bool IsInCurrentTab(const RegisteredShortcut& shortcut)
    {
        auto groupFilter = _tabs[_currentTabIndex].IdGroup;
        auto group = shortcut.GetTopLevelGroup();
        if (groupFilter.empty())
        {
            // Check it doesn't belong in any other tab
            for (const auto& tab : _tabs)
            {
                if (!tab.IdGroup.empty())
                {
                    if (tab.IdGroup == group)
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        else
        {
            return group == groupFilter;
        }
    }

    void InitialiseList()
    {
        auto groupFilter = _tabs[_currentTabIndex].IdGroup;

        _list.clear();

        size_t index = 0;
        std::string group;
        auto& shortcutManager = GetShortcutManager();
        for (auto& shortcut : shortcutManager.Shortcuts)
        {
            if (IsInCurrentTab(shortcut))
            {
                if (group.empty())
                {
                    group = shortcut.GetGroup();
                }
                else
                {
                    auto groupName = shortcut.GetGroup();
                    if (group != groupName)
                    {
                        // Add separator
                        group = groupName;
                        _list.emplace_back();
                    }
                }

                ShortcutStringPair ssp;
                ssp.ShortcutIndex = index;
                ssp.ShortcutId = shortcut.Id;
                ssp.StringId = shortcut.LocalisedName;
                ssp.CustomString = shortcut.CustomName;
                ssp.Binding = FormatKeyChordsString(shortcut);
                _list.push_back(std::move(ssp));
                index++;
            }
        }

        Invalidate();
    }

    void InitialiseTabs()
    {
        _tabs.clear();
        _tabs.push_back({ "interface", SPR_TAB_GEARS_0, 2, 4 });
        _tabs.push_back({ "view", SPR_G2_VIEW, 0, 0 });
        _tabs.push_back({ "window", SPR_TAB_PARK_ENTRANCE, 0, 0 });
        _tabs.push_back({ {}, SPR_TAB_WRENCH_0, 2, 16 });
    }

    void InitialiseWidgets()
    {
        enabled_widgets = (1 << WIDX_CLOSE) | (1 << WIDX_RESET);

        _widgets.clear();
        _widgets.insert(_widgets.begin(), std::begin(window_shortcut_widgets), std::end(window_shortcut_widgets) - 1);

        int32_t x = 3;
        for (size_t i = 0; i < _tabs.size(); i++)
        {
            auto tab = MakeTab({ x, 17 }, STR_NONE);
            _widgets.push_back(tab);
            x += 31;

            enabled_widgets |= (1ULL << (WIDX_TAB_0 + i));
        }

        _widgets.push_back({ WIDGETS_END });
        widgets = _widgets.data();

        WindowInitScrollWidgets(this);
    }

    void SetTab(size_t index)
    {
        if (_currentTabIndex != index)
        {
            _currentTabIndex = index;
            _tabAnimationIndex = 0;
            InitialiseList();
        }
    }

    void ResetAll()
    {
        auto& shortcutManager = GetShortcutManager();
        for (const auto& item : _list)
        {
            auto shortcut = shortcutManager.GetShortcut(item.ShortcutId);
            if (shortcut != nullptr)
            {
                shortcut->Current = shortcut->Default;
            }
        }
        shortcutManager.SaveUserBindings();
        RefreshBindings();
    }

    void DrawTabImages(rct_drawpixelinfo& dpi) const
    {
        for (size_t i = 0; i < _tabs.size(); i++)
        {
            DrawTabImage(dpi, i);
        }
    }

    void DrawTabImage(rct_drawpixelinfo& dpi, size_t tabIndex) const
    {
        const auto& tabDesc = _tabs[tabIndex];
        auto widgetIndex = static_cast<rct_widgetindex>(WIDX_TAB_0 + tabIndex);
        if (!IsWidgetDisabled(widgetIndex))
        {
            auto imageId = tabDesc.ImageId;
            if (imageId != 0)
            {
                if (tabIndex == _currentTabIndex && tabDesc.ImageDivisor != 0 && tabDesc.ImageNumFrames != 0)
                {
                    auto frame = _tabAnimationIndex / tabDesc.ImageDivisor;
                    imageId += frame % tabDesc.ImageNumFrames;
                }

                const auto& widget = widgets[widgetIndex];
                gfx_draw_sprite(&dpi, imageId, windowPos + ScreenCoordsXY{ widget.left, widget.top }, 0);
            }
        }
    }

    void DrawSeparator(rct_drawpixelinfo& dpi, int32_t y, int32_t scrollWidth)
    {
        const int32_t top = y + (SCROLLABLE_ROW_HEIGHT / 2) - 1;
        gfx_fill_rect(&dpi, { { 0, top }, { scrollWidth, top } }, ColourMapA[colours[0]].mid_dark);
        gfx_fill_rect(&dpi, { { 0, top + 1 }, { scrollWidth, top + 1 } }, ColourMapA[colours[0]].lightest);
    }

    void DrawItem(
        rct_drawpixelinfo& dpi, int32_t y, int32_t scrollWidth, const ShortcutStringPair& shortcut, bool isHighlighted)
    {
        auto format = STR_BLACK_STRING;
        if (isHighlighted)
        {
            format = STR_WINDOW_COLOUR_2_STRINGID;
            gfx_filter_rect(&dpi, 0, y - 1, scrollWidth, y + (SCROLLABLE_ROW_HEIGHT - 2), FilterPaletteID::PaletteDarken1);
        }

        auto bindingOffset = scrollWidth - 150;
        auto ft = Formatter();
        ft.Add<rct_string_id>(STR_SHORTCUT_ENTRY_FORMAT);
        if (shortcut.CustomString.empty())
        {
            ft.Add<rct_string_id>(shortcut.StringId);
        }
        else
        {
            ft.Add<rct_string_id>(STR_STRING);
            ft.Add<const char*>(shortcut.CustomString.c_str());
        }
        DrawTextEllipsised(&dpi, { 0, y - 1 }, bindingOffset, format, ft, COLOUR_BLACK);

        if (!shortcut.Binding.empty())
        {
            ft = Formatter();
            ft.Add<rct_string_id>(STR_STRING);
            ft.Add<const char*>(shortcut.Binding.c_str());
            DrawTextEllipsised(&dpi, { bindingOffset, y - 1 }, 150, format, ft, COLOUR_BLACK);
        }
    }

    static std::string FormatKeyChordsString(const RegisteredShortcut& shortcut)
    {
        std::string result;
        auto numChords = shortcut.Current.size();
        for (size_t i = 0; i < numChords; i++)
        {
            const auto& kc = shortcut.Current[i];
            result += kc.ToString();
            if (i < numChords - 1)
            {
                result += " or ";
            }
        }
        return result;
    }
};

void ChangeShortcutWindow::NotifyShortcutKeysWindow()
{
    auto w = window_find_by_class(WC_KEYBOARD_SHORTCUT_LIST);
    if (w != nullptr)
    {
        static_cast<ShortcutKeysWindow*>(w)->RefreshBindings();
    }
}

rct_window* window_shortcut_keys_open()
{
    auto w = window_bring_to_front_by_class(WC_KEYBOARD_SHORTCUT_LIST);
    if (w == nullptr)
    {
        w = WindowCreate<ShortcutKeysWindow>(WC_KEYBOARD_SHORTCUT_LIST, WW, WH, WF_RESIZABLE);
    }
    return w;
}
