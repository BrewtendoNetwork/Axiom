.PHONY: all clean

OUT_FOLDER      := out

PATCHES_OUT_FOLDER := $(OUT_FOLDER)/patches_out
CIA_OUT_FOLDER  := $(OUT_FOLDER)/cia_out
3DSX_OUT_FOLDER := $(OUT_FOLDER)/3dsx_out
COMBINED_OUT_FOLDER := $(OUT_FOLDER)/combined_out

3DS_OUT         := 3ds
CIA_OUT         := cias

AXIOM_UPDATE_OUT   := 3ds/axiom/update

FRIENDS_TITLE_ID    := 0004013000003202
ACT_TITLE_ID        := 0004013000003802
HTTP_TITLE_ID       := 0004013000002902
SOCKET_TITLE_ID     := 0004013000002E02
SSL_TITLE_ID        := 0004013000002F02
MIIVERSE_ID_JPN     := 000400300000BC02
MIIVERSE_ID_USA     := 000400300000BD02
MIIVERSE_ID_EUR     := 000400300000BE02

ACT_OUT             := $(AXIOM_UPDATE_OUT)/$(ACT_TITLE_ID).ips
FRIENDS_OUT         := $(AXIOM_UPDATE_OUT)/$(FRIENDS_TITLE_ID).ips
HTTP_OUT            := $(AXIOM_UPDATE_OUT)/$(HTTP_TITLE_ID).ips
SOCKET_OUT          := $(AXIOM_UPDATE_OUT)/$(SOCKET_TITLE_ID).ips
SSL_OUT             := $(AXIOM_UPDATE_OUT)/$(SSL_TITLE_ID).ips
MIIVERSE_OUT_JPN    := $(AXIOM_UPDATE_OUT)/$(MIIVERSE_ID_JPN).ips
MIIVERSE_OUT_USA    := $(AXIOM_UPDATE_OUT)/$(MIIVERSE_ID_USA).ips
MIIVERSE_OUT_EUR    := $(AXIOM_UPDATE_OUT)/$(MIIVERSE_ID_EUR).ips
PLUGIN_OUT          := $(AXIOM_UPDATE_OUT)/axiom.elf

all:
	@rm -rf $(OUT_FOLDER)

# make patches + app folders
	@mkdir -p $(PATCHES_OUT_FOLDER)/$(AXIOM_UPDATE_OUT)
	@touch $(PATCHES_OUT_FOLDER)/$(AXIOM_UPDATE_OUT)/update.txt
	@mkdir -p $(3DSX_OUT_FOLDER) $(CIA_OUT_FOLDER)/$(CIA_OUT) $(COMBINED_OUT_FOLDER)/$(CIA_OUT)
	
# build patches
	@$(MAKE) -C patches
	
# copy patches to patches folders
	@if [ -d patches/act/out ] && [ "$(ls -A patches/act/out 2>/dev/null)" ]; then \
		cp -r patches/act/out/* $(PATCHES_OUT_FOLDER)/$(ACT_OUT); \
	else \
		echo "Skipping copy for act patch: no output found"; \
	fi
	@if [ -d patches/friends/out ] && [ "$(ls -A patches/friends/out 2>/dev/null)" ]; then \
		cp -r patches/friends/out/* $(PATCHES_OUT_FOLDER)/$(FRIENDS_OUT); \
	else \
		echo "Skipping copy for friends patch: no output found"; \
	fi
	@if [ -d patches/http/out ] && [ "$(ls -A patches/http/out 2>/dev/null)" ]; then \
		cp -r patches/http/out/* $(PATCHES_OUT_FOLDER)/$(HTTP_OUT); \
	else \
		echo "Skipping copy for http patch: no output found"; \
	fi
	@if [ -d patches/socket/out ] && [ "$(ls -A patches/socket/out 2>/dev/null)" ]; then \
		cp -r patches/socket/out/* $(PATCHES_OUT_FOLDER)/$(SOCKET_OUT); \
	else \
		echo "Skipping copy for socket patch: no output found"; \
	fi
	@if [ -d patches/ssl/out ] && [ "$(ls -A patches/ssl/out 2>/dev/null)" ]; then \
		cp -r patches/ssl/out/* $(PATCHES_OUT_FOLDER)/$(SSL_OUT); \
	else \
		echo "Skipping copy for ssl patch: no output found"; \
	fi
	@if [ -d patches/miiverse/out ] && [ "$(ls -A patches/miiverse/out 2>/dev/null)" ]; then \
		cp -r patches/miiverse/out/* $(PATCHES_OUT_FOLDER)/$(MIIVERSE_OUT_JPN); \
		cp -r patches/miiverse/out/* $(PATCHES_OUT_FOLDER)/$(MIIVERSE_OUT_USA); \
		cp -r patches/miiverse/out/* $(PATCHES_OUT_FOLDER)/$(MIIVERSE_OUT_EUR); \
	else \
		echo "Skipping copy for miiverse patch: no output found"; \
	fi
	@cp -r patches/miiverse/*.pem $(PATCHES_OUT_FOLDER)/$(AXIOM_UPDATE_OUT) 2>/dev/null || true

# build plugin
	@$(MAKE) -C plugin

# copy plugin to patches folder
	@cp -r plugin/plugin.elf $(PATCHES_OUT_FOLDER)/$(PLUGIN_OUT)
	
# copy patches output to all 3 output folders
	@find $(PATCHES_OUT_FOLDER) -mindepth 1 -maxdepth 1 -exec cp -r {} $(3DSX_OUT_FOLDER) \; 2>/dev/null || true
	@find $(PATCHES_OUT_FOLDER) -mindepth 1 -maxdepth 1 -exec cp -r {} $(CIA_OUT_FOLDER) \; 2>/dev/null || true
	@find $(PATCHES_OUT_FOLDER) -mindepth 1 -maxdepth 1 -exec cp -r {} $(COMBINED_OUT_FOLDER) \; 2>/dev/null || true

# remove patches folder
	@rm -rf $(PATCHES_OUT_FOLDER)

# build and copy the 3dsx version of the app
	@$(MAKE) -C app 3dsx
	@echo copied 3dsx to 3dsx/combined out folder...
	@cp app/*.3dsx $(3DSX_OUT_FOLDER)/$(3DS_OUT)
	@cp app/*.3dsx $(COMBINED_OUT_FOLDER)/$(3DS_OUT)
	
# build and copy the cia version of the app
	@$(MAKE) -C app cia
	@echo copied cia to cia/combined out folder...
	@cp app/*.cia $(CIA_OUT_FOLDER)/$(CIA_OUT)
	@cp app/*.cia $(COMBINED_OUT_FOLDER)/$(CIA_OUT)

clean:
	@$(MAKE) -C patches clean
	@$(MAKE) -C plugin clean
	@$(MAKE) -C app clean
	@rm -rf $(OUT_FOLDER)
