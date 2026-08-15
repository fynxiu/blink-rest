PROJECT := BlinkRest.xcodeproj
SCHEME := BlinkRest
CONFIGURATION ?= Release
DERIVED_DATA ?= /private/tmp/BlinkRestDerivedData
APP_NAME := BlinkRest.app
BUILD_APP := $(DERIVED_DATA)/Build/Products/$(CONFIGURATION)/$(APP_NAME)
INSTALL_APP ?= /Applications/$(APP_NAME)

.PHONY: build test install uninstall run clean publish publish-dry-run

build:
	xcodebuild -project $(PROJECT) -scheme $(SCHEME) \
		-configuration $(CONFIGURATION) \
		-destination 'platform=macOS' \
		-derivedDataPath $(DERIVED_DATA) \
		CODE_SIGNING_ALLOWED=NO build
	codesign --force --deep --sign - $(BUILD_APP)

test:
	xcodebuild test -quiet -project $(PROJECT) -scheme $(SCHEME) \
		-configuration Debug \
		-destination 'platform=macOS' \
		-derivedDataPath $(DERIVED_DATA)-Tests \
		CODE_SIGNING_ALLOWED=NO \
		-only-testing:BlinkRestTests

install: build
	@echo "Installing $(APP_NAME) to $(INSTALL_APP)"
	@pkill -x BlinkRest 2>/dev/null || true
	rm -rf $(INSTALL_APP)
	ditto $(BUILD_APP) $(INSTALL_APP)
	@echo "Installed: $(INSTALL_APP)"

uninstall:
	@pkill -x BlinkRest 2>/dev/null || true
	rm -rf $(INSTALL_APP)

run: install
	open $(INSTALL_APP)

clean:
	xcodebuild -project $(PROJECT) -scheme $(SCHEME) \
		-configuration $(CONFIGURATION) \
		-derivedDataPath $(DERIVED_DATA) clean

publish:
	@test -n "$(VERSION)" || (echo "Usage: make publish VERSION=1.2.3 [PLATFORMS=macos|windows|both]" >&2; exit 2)
	./scripts/publish.sh --platforms "$(or $(PLATFORMS),both)" "$(VERSION)"

publish-dry-run:
	@test -n "$(VERSION)" || (echo "Usage: make publish-dry-run VERSION=1.2.3 [PLATFORMS=macos|windows|both]" >&2; exit 2)
	./scripts/publish.sh --dry-run --platforms "$(or $(PLATFORMS),both)" "$(VERSION)"
