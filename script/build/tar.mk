# 标志位
.PHONY: FORCE_TAR
FORCE_TAR:

$(path-attach)/%.tar : $(path-bin)/% FORCE_TAR
	$(call prepare, $@)
	tar -C $(dir $<) -cf $@ $(notdir $<)
# 	cp $@ $(path-bin)
