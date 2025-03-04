DOXYFILE := $(DOCS_DIR)/doxygen/Doxyfile

.PHONY: docs
docs: $(DOXYFILE)
	$(call ASSERT_EXE_EXISTS,doxygen)
	$(SILENT)doxygen $(DOXYFILE)
