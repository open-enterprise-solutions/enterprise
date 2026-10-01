#include "metaModuleObject.h"

/* PNG */
static const wxString s_module_64_png = wxT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAABrElEQVR4nOybsUrEQBCG/z19BYtDJbZaWESFQ5vYi6D4CAqWgoVgIwjX22tr5UN4jXAgprBQBAtFxUewc8yBQkB0EzJ7ydzM1yWZYvOT/8/OJtuCclpQjgkA5Yz7ChaXky0iOnbOzaJGsjHcE7lu2u+dg5ExX0F7KrrIbn4ONZONYcI5bLYnZ97eX59SMOG1QBNuPo9r4XShk2yDCZEZ8C3CLhhwvoIsAyh/fH11iWGytLL65zX6xM5Nv3eGCoh+C3DYQfxrsKoI4gQ4PNj/da6KCOIE2FhfYxVBpAU4RRCbAVwiiA5BDhG8vUDT+G9ekGcgQtxJPny9w0h3g1nv0PXViHsCypAJEPlqbD0ADads71E0I36wJTEoxzIATDzHMTiIUrbVrkKYBaAcywAwMWzvcmEWgHIsAxCY6b1bhODlZB4cmAWgHMsABIbLq6EwC0A5lgEIDNc8IFSWmAWgHMsABMbmAQ3HBIByav8uUPdaolkAyrEMABP2XUAoJgCUUzoDyv6D03TMAr4CIrqDUIqMvcimqSMiPEIeD4Ox+4q8e4ZGHcsAKEe9AF8AAAD//38UW8gAAAAGSURBVAMA4FFiI/jPFoYAAAAASUVORK5CYII=");

wxIcon ibValueMetaObjectModuleBase::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectModuleBase::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_module_64_png, wxSize(16, 16));

	return icon;
}

wxIcon ibValueMetaObjectCommonModule::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectCommonModule::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_module_64_png, wxSize(16, 16));

	return icon;
}

wxIcon ibValueMetaObjectManagerModule::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectManagerModule::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_module_64_png, wxSize(16, 16));

	return icon;
}
