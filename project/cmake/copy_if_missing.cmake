# Kopiert SRC nach DST, aber nur wenn DST noch nicht existiert.
# (Spieler-Einstellungen aus dem Launcher sollen einen Neubau ueberleben.)
if(NOT EXISTS "${DST}")
    file(COPY_FILE "${SRC}" "${DST}")
    message(STATUS "Lege ${DST} an")
endif()
