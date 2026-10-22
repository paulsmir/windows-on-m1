[Environment]::SetEnvironmentVariable('APPLE_AGX_UMD_TRACE_FILE',$null,'User')
"ENV_AFTER=" + [Environment]::GetEnvironmentVariable('APPLE_AGX_UMD_TRACE_FILE','User')
