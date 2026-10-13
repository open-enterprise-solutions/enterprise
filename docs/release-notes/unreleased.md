# Unreleased

An old binary on the same base still acquires `sys_lock` without the `sys_lock_key` header row. Until every process is upgraded, two processes — one of them not upgraded — can both be granted the same key.
