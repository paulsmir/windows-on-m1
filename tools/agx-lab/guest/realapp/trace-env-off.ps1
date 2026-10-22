[Environment]::SetEnvironmentVariable('APPLE_AGX_UMD_TRACE_FILE',$null,'User')
[Environment]::SetEnvironmentVariable('APPLE_AGX_D3D12_SHADER_DUMP_DIR',$null,'User')
"ENV_AFTER=" + [Environment]::GetEnvironmentVariable('APPLE_AGX_UMD_TRACE_FILE','User') + [Environment]::GetEnvironmentVariable('APPLE_AGX_D3D12_SHADER_DUMP_DIR','User')
