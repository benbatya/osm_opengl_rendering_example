# generate_config.cmake
# This script is run at build time via add_custom_command
file(READ ${CS_FILE} COMPUTE_SHADER)
file(READ ${VS_FILE} VERTEX_SHADER)
file(READ ${FS_FILE} FRAGMENT_SHADER)

# # Run configure_file
# # The @ONLY option ensures only @VAR@ syntax is expanded, not ${VAR}
configure_file(
    ${IN_FILE}
    ${OUT_FILE}
    @ONLY
)