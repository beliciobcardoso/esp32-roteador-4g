FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

# http/index.html e a pagina de configuracao. Ela entra no binario como esta, via
# EMBED_TXTFILES, em vez de ser copiada para um raw string em html_page.cpp: duas copias
# do mesmo HTML divergem na primeira edicao que esquece a segunda. O linker expoe o
# conteudo em _binary_index_html_start, declarado por src/adapters/html_page.h.
# EMBED_TXTFILES (e nao EMBED_FILES) porque so ele acrescenta o '\0' final que o
# String(const char*) do Arduino precisa para saber onde a pagina termina.
idf_component_register(
    SRCS ${app_sources}
    EMBED_TXTFILES ${CMAKE_SOURCE_DIR}/http/index.html
)
