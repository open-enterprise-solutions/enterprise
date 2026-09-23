#include "metaComposerObject.h"
#include "backend/backend_picture.h"   // ibBackendPicture::GetIconFromBase64

/* PNG */
// The composer's own picture (Max, 2026-08-20) — 80×80, scaled to the tree's 16 on load, which is
// how every other metatype's resource here is stored. It replaces the template sheet that stood in
// while there was nothing to give it.
static const wxString s_composer_80_png = wxT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAACvElEQVR4nOyazWsTQRjGn7c19SgKajA5eVOUBMQP8KZU8VbUYwsexINg6l8g6H+Q6s2LB3sr1B5L670oYiMFb55SE4v4cWxs8zobjG66SzozzkfLzO8QMpudfWee3Zl9Z/KMIHBGEDhRAATOAVikVFudIqIJMF8C0QmlysyfRZ0V8WW+Wa++hCUIFjh6f604VtieFRe/AgMweOkXRqc26me/wDBWhsBYYWvOVOcTCDRe4O4sLGBcgHKtcVc0+DIMQ4SrpYcf7sAwNuaAW+nCmZOHcP1CEccOH4QKG983sfimjbVPP/8d7PJN8fkCBrExBKrpwjWNzickdcbPFweOEfgcDGP+CSAMtPq4Ruf7FI/sqKv6JpEgJkIInCgAAicKgMCJAsA0jHa62P62CV2ydbkFw5gXgPA+XVx628bXHx2okqTCSd00DLyDYYxngtvMz0eJbvTLSS4/kM//gUVv/i7GubfY2ZUu8AyGMf4EtGaq86Jzr2XOJShsSDAWWvXqIgxjZRLsbI1OChGWh52TvuO73f3ehkgH92ABKztCfcrTq5Oi9ckS9qL6QobXGbQiVoCv9t2WmAzl6Qany816xUtbYh6AwIkCIHCCF0B75i09aNwW8j0RFzgFj4hXyUeRIj5af1qZgwbaTwARP/bd+V47RBuStkAT/bUA0WnsFf6jLfHfYRjCdSa3M5PUJb4GEThRAAROzASH/ejb4+Mifq4Avj0+LuPnDgEZjw9z/vc8VD0+LuNnBFDx+CSBZbMRWY+P6/h5maBvj4/T+HlDwLfHx2n87BPg2+PjOH5MhBA4UQAEThQAgRMFyBzx7fFxHD8rgG+Pj+P4mUxQ1uOjg4zHx3X83P2AUq2xnKyeYBLGQnOmMiFzqsv4uZOgjMdHLbaax8dl/KFbYr49Pi7ie/MI7RViHoDACV6A3wAAAP//j0CxCAAAAAZJREFUAwBUJ2l9VYR+kQAAAABJRU5ErkJggg==");

// A composer's icon — the scheme it IS: a declaration of what to read and how to fold it.
wxIcon ibValueMetaObjectComposer::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectComposer::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_composer_80_png, wxSize(16, 16));

	return icon;
}
