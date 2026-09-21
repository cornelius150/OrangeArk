
add_compile_options(-Wall -Wextra -Wunreachable-code -Wuninitialized)

# Generating icons
execute_process(
  COMMAND ${PYTHON_EXEC} ${CMAKE_SOURCE_DIR}/scripts/gresource_to_c.py
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Generating rc file (with icon) for windows
if(MINGW)
  # OrangeArk: the version resource used to be compiled exactly once — without
  # DEPENDS, editing data/windows.rc never forced a rebuild, so the "Product
  # name" shown by Explorer / Task Manager / the installer kept the old value.
  # -c 65001 because windows.rc is UTF-8 and carries Chinese text.
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/orangeark_rc.o
                     COMMAND windres.exe --codepage=65001
                                         -I${CMAKE_SOURCE_DIR}/icons
                                         -i${CMAKE_SOURCE_DIR}/data/windows.rc
                                         -o ${CMAKE_CURRENT_BINARY_DIR}/orangeark_rc.o
                     DEPENDS ${CMAKE_SOURCE_DIR}/data/windows.rc
                             ${CMAKE_SOURCE_DIR}/icons/orangeark.ico
                     VERBATIM)
  set(CHERRY_EXE_RC ${CMAKE_CURRENT_BINARY_DIR}/orangeark_rc.o)
endif(MINGW)

set(CT_SHARED_FILES
  ct_actions_edit.cc
  ct_actions_export.cc
  ct_actions_file.cc
  ct_actions_find.cc
  ct_actions_format.cc
  ct_actions_others.cc
  ct_actions_import.cc
  ct_actions_tree.cc
  ct_actions_view.cc
  ct_actions_help.cc
  ct_app.cc
  ct_clipboard.cc
  ct_codebox.cc
  ct_config.cc
  ct_dialogs.cc
  ct_dialogs_anch_widg.cc
  ct_dialogs_cmd_palette.cc
  ct_dialogs_sel_node.cc
  ct_dialogs_find.cc
  ct_dialogs_gen_purp.cc
  ct_dialogs_link.cc
  ct_dialogs_tree.cc
  ct_export2html.cc
  ct_export2pdf.cc
  ct_export2txt.cc
  ct_image.cc
  ct_screenshot.cc
  ct_imports.cc
  ct_list.cc
  ct_main_win.cc
  ct_main_win_buffer.cc
  ct_main_win_events.cc
  ct_main_win_file.cc
  ct_main_win_vte.cc
  ct_menu.cc
  ct_menu_actions.cc
  ct_menu_ui.cc
  ct_misc_utils.cc
  ct_p7za_iface.cc
  ct_pref_dlg.cc
  ct_pref_dlg_kb_shortcuts.cc
  ct_pref_dlg_misc.cc
  ct_pref_dlg_plain_text_n_code.cc
  ct_pref_dlg_rich_text.cc
  ct_pref_dlg_special_chars.cc
  ct_pref_dlg_text_n_code.cc
  ct_pref_dlg_theme.cc
  ct_pref_dlg_toolbar.cc
  ct_pref_dlg_tree.cc
  ct_state_machine.cc
  ct_storage_control.cc
  ct_storage_sqlite.cc
  ct_storage_xml.cc
  ct_storage_multifile.cc
  ct_storage_md.cc
  ct_table.cc
  ct_table_light.cc
  ct_treestore.cc
  ct_widgets.cc
  ct_text_view.cc
  ct_parser_text.cc
  ct_parser_md.cc
  ct_parser_html.cc
  ct_parser.cc
  ct_filesystem.cc
  ct_column_edit.cc
)

add_library(orangeark_shared STATIC ${CT_SHARED_FILES})
target_link_libraries(orangeark_shared
  ${GTKMM_LIBRARIES}
  ${GTKSV_LIBRARIES}
  ${GLIBMM_LIBRARIES}
  ${PANGOMM_LIBRARIES}
  ${CT_LIBXML_LIBRARIES}
  ${SQLITE_LIBRARIES}
  ${CURL_LIBRARIES}
  ${UCHARDET_LIBRARIES}
  ${FRIBIDI_LIBRARIES}
  pthread
  7za_static
)
if(CT_USE_GSPELL)
  target_link_libraries(orangeark_shared
    ${GSPELL_LIBRARIES}
  )
endif()
if(CT_USE_LIBSPELLING)
  target_link_libraries(orangeark_shared
    ${LIBSPELLING_LIBRARIES}
  )
endif()
if(USE_SHARED_FMT_SPDLOG)
  target_link_libraries(orangeark_shared
    ${FMT_LIBRARIES}
    ${SPDLOG_LIBRARIES}
  )
else()
  target_link_libraries(orangeark_shared
    spdlog::spdlog
  )
endif()
if(USE_VTE)
  target_link_libraries(orangeark_shared
    ${VTE_LIBRARIES}
  )
endif()

add_executable(orangeark ct_main.cc icons.gresource.cc ${CHERRY_EXE_RC})
target_link_libraries(orangeark orangeark_shared)

if(APPLE)
  target_link_libraries(orangeark "-framework Foundation -framework AppKit")
elseif(WIN32)
  target_link_libraries(orangeark "-mwindows")
endif()

set_target_properties(orangeark PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# install orangeark
install(TARGETS orangeark RUNTIME DESTINATION bin)
