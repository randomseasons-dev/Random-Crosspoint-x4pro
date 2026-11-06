#pragma once

#include <I18n.h>
#include <Xtc.h>

#include <memory>
#include <string>

#include "CrossPointSettings.h"
#include "EpubReaderMenuActivity.h"
#include "ReaderActivity.h"

class XtcReaderActivity final : public ReaderActivity {
  std::shared_ptr<Xtc> xtc;
  uint32_t currentPage = 0;
  // Per-book orientation (CrossPointSettings::ORIENTATION), saved in the book's
  // cache dir when changed from the menu; ORIENTATION_COUNT = follow Settings.
  uint8_t bookOrientation = CrossPointSettings::ORIENTATION_COUNT;
  // Last highlighted row of the reader menu (restored when it reopens).
  EpubReaderMenuActivity::MenuAction selectedMenuAction = EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER;

  enum class StatusBarOverlayPosition { Bottom, Top };
  struct StatusBarInfo {
    int currentPage;
    int pageCount;
    std::string title;
  };

  void renderPage();
  void showPageMessage(StrId message);
  void openChapterSelection();
  void openReaderMenu();
  uint8_t currentOrientation() const;
  void loadBookOrientation();
  void saveBookOrientation(uint8_t orientation);
  void renderStatusBarOverlay(GfxRenderer& renderer, StatusBarOverlayPosition position) const;
  StatusBarInfo getStatusBarInfo() const;
  void saveProgress() const;
  void loadProgress();

  bool loadBook() override;
  std::string getBookTitle() const override { return xtc ? xtc->getTitle() : ""; }
  std::string getBookAuthor() const override { return xtc ? xtc->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return xtc ? xtc->getThumbBmpPath() : ""; }
  bool isPictureBook() const override { return true; }
  std::string getBookCachePath() const override { return xtc ? xtc->getCachePath() : ""; }
  bool handleFormatInput() override;
  void renderBook() override;

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("XtcReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~XtcReaderActivity() override = default;

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
