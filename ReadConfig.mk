
# Function for rescuing variables
define RESCUE
 $(1)_rescued:=$$($(1))
endef

define RESTORE
 ifneq ($$($(1)_rescued),)
  $(1):=$$($(1)_rescued)
 endif
endef

# First rescue all variables
$(foreach V,$(RESCUED_VARS),$(eval $(call RESCUE,$(V))))

# Read config
-include config.local

# Then restore rescued variables
$(foreach V,$(RESCUED_VARS),$(eval $(call RESTORE,$(V))))