#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/blocks.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

namespace {
// rs5 home grid geometry (portrait 480x800 -> 4 covers x 3 rows above a 2 x 2 button grid).
constexpr int kCoverPad = 6;        // selection frame around a cover
constexpr int kBarGap = 4;          // gap around the progress bar
constexpr int kBarHeight = 5;       // progress bar under each cover
constexpr int kMinTileWidth = 108;  // narrowest cover tile; sets the column count
constexpr int kButtonHeight = 52;
constexpr int kButtonGap = 8;
constexpr int kCornerRadius = 6;
constexpr int kIconSize = 32;
constexpr int kIconTextGap = 8;
}  // namespace

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // File Browser, Library, File transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasLibrarySlot()) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

HomeActivity::HomeLayout HomeActivity::computeLayout() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getContentArea(renderer);
  const int pageHeight = renderer.getScreenHeight();
  HomeLayout l;

  // Buttons: 2 x 2 (3 + 2 when the Plugins/OPDS slot is shown), pinned above the button hints.
  l.menuCount = hasLibrarySlot() ? 5 : 4;
  l.menuCols = l.menuCount <= 4 ? 2 : 3;
  const int menuRows = (l.menuCount + l.menuCols - 1) / l.menuCols;
  const int menuW = content.width - 2 * metrics.contentSidePadding;
  l.menuX = content.x + metrics.contentSidePadding;
  l.cellW = (menuW - (l.menuCols - 1) * kButtonGap) / l.menuCols;
  const int menuH = menuRows * kButtonHeight + (menuRows - 1) * kButtonGap;
  l.menuY = pageHeight - metrics.buttonHintsHeight - metrics.menuSpacing - menuH;

  // Covers: the band between the header and the buttons; cover edges line up with the buttons.
  l.areaX = l.menuX - kCoverPad;
  l.areaW = menuW + 2 * kCoverPad;
  l.areaY = metrics.homeTopPadding;
  l.areaH = std::max(0, l.menuY - metrics.menuSpacing - l.areaY);
  l.cols = std::max(1, l.areaW / kMinTileWidth);
  l.tileW = l.areaW / l.cols;
  l.coverW = l.tileW - 2 * kCoverPad;
  const int below = kBarGap + kBarHeight + kBarGap + renderer.getLineHeight(SMALL_FONT_ID) + kCoverPad;
  // Rows at a book-shaped cover (2:3), allowing a little shrink to fit one more row;
  // then the covers grow to fill the band exactly (no empty strip).
  const int naturalTile = kCoverPad + l.coverW * 3 / 2 + below;
  l.rows = std::max(1, (l.areaH + naturalTile / 8) / naturalTile);
  l.tileH = l.areaH / l.rows;
  l.coverH = std::max(20, std::min(l.tileH - kCoverPad - below, l.coverW * 8 / 5));
  return l;
}

int HomeActivity::coverThumbHeight(const HomeLayout& l) const {
  // Thumbs fit a (0.6*h, h) box; ask for one tall enough that its width covers the slot.
  return std::max(l.coverH, l.coverW * 5 / 3 + 2);
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    const int thumbHeight = coverHeight;
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, thumbHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub/txt/md, try to load the metadata for title/author and cover
        if (FsHelpers::hasReflowableBookExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(thumbHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(thumbHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();
  // Plugin System is the global opt-in. Once enabled, expose the Plugins entry
  // even when no SD plugins are installed yet so Plugin Hub can be installed
  // from inside the Plugins screen (or users can sideload plugins manually).
  hasPlugins = SETTINGS.pluginsEnabled != 0;

  // As many recent books as the cover grid holds.
  const HomeLayout layout = computeLayout();
  loadRecentBooks(std::min(RecentBooksStore::MAX_RECENT_BOOKS, layout.cols * layout.rows));
  hasContinueReading = !recentBooks.empty();

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, hasLibrarySlot());

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();

  auto activateSelection = [this] {
    if (selectorIndex < recentBooks.size()) {
      onRecentBookSelected(recentBooks[selectorIndex].path);
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    switch (indexToMenuItem(menuIndex, hasLibrarySlot())) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::LIBRARY:
        onLibraryOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:  // the library slot
        hasPlugins ? onPluginsOpen() : onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      default:
        break;
    }
  };

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && hasContinueReading && !recentBooks.empty()) {
    onRecentBookSelected(recentBooks[0].path);
    return;
  }

  // Touch: one hit band per grid row, matching the drawn covers and buttons.
  const HomeLayout l = computeLayout();
  const auto handleTouch = [this, &activateSelection](const MappedInputManager::RowTouch touch, const int index) {
    if (touch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != index) {
        selectorIndex = index;
        requestUpdate();
      }
    } else {
      selectorIndex = index;
      haptic_feedback::touchAction();
      activateSelection();
    }
  };
  const int bookCount = static_cast<int>(recentBooks.size());
  for (int row = 0; row * l.cols < bookCount; ++row) {
    const int first = row * l.cols;
    const int y0 = l.areaY + row * l.tileH;
    int col = -1;
    const auto touch =
        mappedInput.colTouch(col, l.areaX, l.tileW, std::min(l.cols, bookCount - first), y0, y0 + l.tileH);
    if (touch != MappedInputManager::RowTouch::None) {
      handleTouch(touch, first + col);
      return;
    }
  }
  for (int row = 0; row * l.menuCols < l.menuCount; ++row) {
    const int first = row * l.menuCols;
    const int y0 = l.menuY + row * (kButtonHeight + kButtonGap);
    int col = -1;
    const auto touch = mappedInput.colTouch(col, l.menuX, l.cellW + kButtonGap, std::min(l.menuCols, l.menuCount - first),
                                            y0, y0 + kButtonHeight, l.cellW);
    if (touch != MappedInputManager::RowTouch::None) {
      handleTouch(touch, bookCount + first + col);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::drawCovers(const HomeLayout& l) {
  const int thumbHeight = coverThumbHeight(l);
  for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
    const int x = l.areaX + (i % l.cols) * l.tileW + kCoverPad;
    const int y = l.areaY + (i / l.cols) * l.tileH + kCoverPad;
    bool hasCover = false;
    if (!recentBooks[i].coverBmpPath.empty()) {
      const std::string thumbPath = UITheme::getCoverThumbPath(recentBooks[i].coverBmpPath, thumbHeight);
      HalFile file;
      if (Storage.openFileForRead("HOME", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          // Fill the slot 1:1 and crop the overflow.
          hasCover = BaseTheme::drawCoverThumbFill(renderer, bitmap, Rect{x, y, l.coverW, l.coverH});
        }
        file.close();
      }
    }
    renderer.drawRect(x, y, l.coverW, l.coverH, true);
    if (!hasCover) {
      renderer.fillRect(x, y + l.coverH / 3, l.coverW, 2 * l.coverH / 3, true);
      renderer.drawIcon(CoverIcon, x + 12, y + 12, 32);
    }
  }
}

void HomeActivity::drawBookLabels(const HomeLayout& l) const {
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int below = kBarGap + kBarHeight + kBarGap + lineHeight + kCoverPad;
  for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
    const int tileX = l.areaX + (i % l.cols) * l.tileW;
    const int tileY = l.areaY + (i / l.cols) * l.tileH;
    const int x = tileX + kCoverPad;
    const int coverBottom = tileY + kCoverPad + l.coverH;

    if (selectorIndex == i) {
      // Light-gray frame around the cover and behind its bar + title (Lyra Extended style).
      renderer.fillRoundedRect(tileX, tileY, l.tileW, kCoverPad, kCornerRadius, true, true, false, false,
                               Color::LightGray);
      renderer.fillRectDither(tileX, tileY + kCoverPad, kCoverPad, l.coverH, Color::LightGray);
      renderer.fillRectDither(tileX + l.tileW - kCoverPad, tileY + kCoverPad, kCoverPad, l.coverH, Color::LightGray);
      renderer.fillRoundedRect(tileX, coverBottom, l.tileW, below, kCornerRadius, false, false, true, true,
                               Color::LightGray);
    }

    // Reading progress line (outline only until the book has been read once on rs5).
    const int barY = coverBottom + kBarGap;
    renderer.drawRect(x, barY, l.coverW, kBarHeight, true);
    const int percent = recentBooks[i].percent;
    if (percent > 0) renderer.fillRect(x, barY, std::max(1, l.coverW * percent / 100), kBarHeight, true);

    const std::string title = renderer.truncatedText(SMALL_FONT_ID, recentBooks[i].title.c_str(), l.coverW);
    renderer.drawText(SMALL_FONT_ID, x, barY + kBarHeight + kBarGap, title.c_str(), true);
  }
}

void HomeActivity::drawMenu(const HomeLayout& l) const {
  // Same order as indexToMenuItem(): Files, Library, [Plugins/OPDS], Transfer, Settings.
  std::vector<const char*> labels = {tr(STR_HOME_FILES), tr(STR_LIBRARY), tr(STR_HOME_TRANSFER),
                                     tr(STR_SETTINGS_TITLE)};
  std::vector<const uint8_t*> icons = {FolderIcon, LibraryIcon, TransferIcon, Settings2Icon};
  if (hasLibrarySlot()) {
    labels.insert(labels.begin() + 2, hasPlugins ? tr(STR_PLUGINS) : tr(STR_HOME_OPDS));
    icons.insert(icons.begin() + 2, BlocksIcon);
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
    const int x = l.menuX + (i % l.menuCols) * (l.cellW + kButtonGap);
    const int y = l.menuY + (i / l.menuCols) * (kButtonHeight + kButtonGap);
    if (selectorIndex == bookCount + i) {
      renderer.fillRoundedRect(x, y, l.cellW, kButtonHeight, kCornerRadius, Color::LightGray);
    }
    renderer.drawRoundedRect(x, y, l.cellW, kButtonHeight, 1, kCornerRadius, true);

    const int maxTextWidth = l.cellW - 16 - kIconSize - kIconTextGap;
    const std::string label = renderer.truncatedText(UI_12_FONT_ID, labels[i], maxTextWidth);
    const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label.c_str());
    const int iconX = x + (l.cellW - (kIconSize + kIconTextGap + textWidth)) / 2;
    renderer.drawIcon(icons[i], iconX, y + (kButtonHeight - kIconSize) / 2, kIconSize);
    renderer.drawText(UI_12_FONT_ID, iconX + kIconSize + kIconTextGap, y + (kButtonHeight - lineHeight) / 2,
                      label.c_str(), true);
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const HomeLayout l = computeLayout();

  // The cover band is snapshotted once drawn, so selection moves don't re-read the covers.
  coverRectX = l.areaX;
  coverRectY = l.areaY;
  coverRectW = l.areaW;
  coverRectH = l.areaH;

  renderer.clearScreen();
  const bool bufferRestored = coverRendered && coverBufferStored && restoreCoverBuffer();

  // Home is the stack root: no back button in its header.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding}, nullptr,
                 nullptr, false);

  if (recentBooks.empty()) {
    constexpr int padding = 48;
    const int midY = l.areaY + l.areaH / 2;
    renderer.drawText(UI_12_FONT_ID, l.areaX + padding, midY - renderer.getLineHeight(UI_12_FONT_ID) - 2,
                      tr(STR_NO_OPEN_BOOK), true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, l.areaX + padding, midY + 2, tr(STR_START_READING), true);
  } else {
    if (!bufferRestored) {
      drawCovers(l);
      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;  // Only consider it rendered if we successfully stored the buffer
    }
    drawBookLabels(l);
  }

  drawMenu(l);

  const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(coverThumbHeight(l));
  }
}

void HomeActivity::onRecentBookSelected(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onLibraryOpen() { activityManager.goToLibrary(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onPluginsOpen() { activityManager.goToPlugins(hasOpdsServers); }
