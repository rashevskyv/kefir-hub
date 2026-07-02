# === Configuration ===
SPHAIRA_ROOT_DIR := ~/dev/sphaira
SPHAIRA_BUILD_PRESET := ReleaseWithInstall

# Шлях до зібраного NRO
SOURCE_NRO_DIR := $(SPHAIRA_ROOT_DIR)/build/$(SPHAIRA_BUILD_PRESET)/switch/sphaira
SOURCE_NRO_FILE := $(SOURCE_NRO_DIR)/sphaira.nro

# Куди копіювати NRO
DEST_DIR := ~/dev/_kefir/kefir
DEST_NRO_FILE := $(DEST_DIR)/hbmenu.nro

# Налаштування nxlink
NXLINK_IP := 192.168.50.69

# === Targets ===
.PHONY: all build copy nxlink clean help

# Команда за замовчуванням (make або make all)
all: copy
	@echo "-------------------------------------------------------"
	@echo "Sphaira built and copied to $(DEST_NRO_FILE)"
	@echo "-------------------------------------------------------"

# Тільки збірка
build:
	@echo ">>> Configuring Sphaira with preset: $(SPHAIRA_BUILD_PRESET)..."
	@cd $(SPHAIRA_ROOT_DIR) && cmake --preset $(SPHAIRA_BUILD_PRESET)
	@echo ">>> Building Sphaira with preset: $(SPHAIRA_BUILD_PRESET)..."
	@cd $(SPHAIRA_ROOT_DIR) && cmake --build --preset $(SPHAIRA_BUILD_PRESET)
	@echo ">>> Build complete. Artifact: $(SOURCE_NRO_FILE)"

# Копіювання результату збірки
copy: build
	@echo ">>> Copying $(SOURCE_NRO_FILE) to $(DEST_NRO_FILE)..."
	@mkdir -p $(DEST_DIR)
	@cp $(SOURCE_NRO_FILE) $(DEST_NRO_FILE)
	@echo ">>> Copied successfully."

# Збірка та відправка через nxlink
nxlink:
	@echo ">>> Sending $(SOURCE_NRO_FILE) to $(NXLINK_IP) via nxlink..."
	@nxlink -a $(NXLINK_IP) $(SOURCE_NRO_FILE)
	@echo ">>> Sent via nxlink."

# Очищення
clean:
	@echo ">>> Cleaning Sphaira build directory: $(SPHAIRA_ROOT_DIR)/build/$(SPHAIRA_BUILD_PRESET)..."
	@rm -rf $(SPHAIRA_ROOT_DIR)/build/$(SPHAIRA_BUILD_PRESET)
	@echo ">>> Cleaning copied NRO: $(DEST_NRO_FILE)..."
	@rm -f $(DEST_NRO_FILE)
	@echo ">>> Clean complete."

# Допомога
help:
	@echo "Available targets:"
	@echo "  make / make all    - Build Sphaira and copy result to $(DEST_NRO_FILE)"
	@echo "  make build         - Only build Sphaira"
	@echo "  make copy          - Build (if needed) and copy result"
	@echo "  make nxlink        - Build (if needed) and send via nxlink to $(NXLINK_IP)"
	@echo "  make clean         - Remove build artifacts and copied NRO"
	@echo "  make help          - Show this help message"
