#include "metaCommonAttributeObject.h"

// THE ATTRIBUTE'S SHAPE AGAIN, in a third colour. A common attribute IS an attribute,
// so the icon keeps the outline; what it has to say is that this one is not owned by
// the object it appears in.
//
// The COPY carries the same picture as the declaration, and that is deliberate: inside
// an object it must read as "an attribute, but not yours" at a glance — the tree is
// where somebody will try to rename or delete it, and the icon is the first thing that
// says where to go instead.
//
// Placeholder in the honest sense: the attribute icon with its channels rotated, good
// enough to tell the branches apart until somebody draws one on purpose.

/* PNG */
static const wxString s_commonAttribute_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAD7ElEQVR4nOyaWUwTQRjH/9NyWjyo4i0k3qDGW4MajxjR+OrxbIznmwkhHnhgUi/ENxNBjb6YGIjxyUQMhBdU8IErEsAomABRFCkUKJSjHWebAJ3dbesuhm67+0va5pv5Ovn268zu/OerCTrHBJ1jJAA6x0gAdI6RAOicKEyS+Nv56WZKTgA0lZlpAJmNkEK72FsDi6PBTfB88MrZT4G8CdSSUxRjie62gdJMQogmZxIF3Ow91zlizUHO8WE5H9WBs4t/z7KXpdWLF2DxmQnIZUuUvdyfj6rgE24V2NjgWxEmsB9pW4It/6ZsHxQSb3u0zUxM3Lpak7wIe9enYv6smZgWG4NQMjA0jI4eB8rqGtDY+oPr85hMWwYun67ybVM8A0yEnPe105IX4mTGbiydlxTyixcQYhBiOZWxB6sXL+D6TB7PGbG/4gSwKbPK196/YS20yoFN68RNq8QNyu8BFNyoCxJnQKskzUjgbEq9j2oO5fsAQrhRY6OjoVUscbGczW6Gc8U+xlYYOsdIAHSOkQDoHOM8IJiDVO/zZD59ialkWlyMV3PMY6+tK5ciZe7kjh/8J2BM73toJtv/miZzdPA/GXANo6Wj0/uq/NKMfUyEHdy4DlFmdZPZ77fCQe+zrS3Kahvw8E0J1CJ7ceGm99s67Siu+gw1SJaAoPfZR7Zv2+EVKbiQvh6pc6yYFRdaydvDlkDjHzsefKzFu2+t4+0lNfVYk7IIS1iMSpDMALHeP7Q8GYXHDiJ98fyQX7yAEIMQy6vjh5CxbAnXV9nUDKVIEiDW+1k7N0GrXNy1mbM7Hb1QivQeINL7aUmJ0CrLrTM5u6PbAaVIH4MivZ8Qo129b43n9b7TNRTQZk+NDvEYky6MaJnfvX18AyFVYp+ITkBpdT1nE0prxD4Rl4B+Nu07HX0ora1HU/vP8XYKOuim5mdi/4hLwI0Xr+U7KK67rp3+Lm6O6CUwAa1wXj2XJ9ejOgHTbz+GGvqunFE1nr/vBcJbHKW47xy13vDnE4EzYKI87hHK49mBy+ORdxPMPjdHib9xIgSVqFmTUznev2IcikLnGAmAzjESIG4Qa2b74BC0ijg2Ob0fDOkMIISTjF/tPdAqkthk9H4w5I7E6nzNvA810Cr3yqs5W07vB0OSALfZ/YTVG1xjdjE7ej5aVIzK9l9wDA0j1HQNuryxHCl8i5KWtvF2Qe+Pyuj9YMjWuyy2gkuE4A7CCLb+s/xJ3kD4LfhZbhVUss7tCAtoBRNBO6ACv49B50jibrYUcllmPdAogt5nMd7tH7HuhUqClnzD/e/wwdBGzTuEGDtB6BwjAdA5RgKgc3SfgL8AAAD//8FSVesAAAAGSURBVAMAznk3XxADTdUAAAAASUVORK5CYII=");

wxIcon ibValueMetaObjectCommonAttribute::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectCommonAttribute::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_commonAttribute_16_png, wxSize(16, 16));

	return icon;
}

wxIcon ibValueMetaObjectCommonAttributeColumn::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectCommonAttributeColumn::GetIconGroup()
{
	// The declaration's — same thing, seen from inside an object.
	return ibValueMetaObjectCommonAttribute::GetIconGroup();
}
