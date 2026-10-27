#include "textBox.h"

#include "backend/backend_picture.h"

static const ibString s_textBox_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAABhUlEQVR4nOybsU4CURBF72z8A8slsZYCS0v/wfgJmmgBlpZGEwpbLSy01P9xKTQmlvyDHc9FLUgIDJsdlh3mnoIEeMXbk527My/ZDMHJEBwKQHB2tAV5vzgpNd0KsI8NkoAPmchw/NB7hSHqHSCSbjZ98b/7mO4hSy+dQXEKQ/QSEOmiTQieLCX4zIBSQj54O4cBoi3oXBZp9vvdRQ9NcvU4Wvxnwtn4/uAZNfD9FDAoB/+PwZoS3Ak4Psrnf6whwZ2Aw+6uqQSXJWApwW0GWElwHYIWEtRZoG0s7QtmmUroj7612WGrp8Ekk6G2xt0dUAUR2dPW8DwALafq7LFyRvzDIzEEhxkAI6rWnkZT5w4sAQSHGQAjmj4rtIIlgOAwA2CEdR+gYZU5LAEEhxkAI9gHOIUCEBz2AQgOBSA47AMQHApAcFrXBzSdJSwBBIcZACPYBziFAhCcyhnQ9Ny/blgC6oqU3uGVFfauCkhJrsvPLzgjAZ9/e1+O+s7QtsMMQHDCC/gBAAD//0qcl2UAAAAGSURBVAMAIYde1O8QyCcAAAAASUVORK5CYII=";

ibPicture ibValueTextBox::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueTextBox::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_textBox_png, ibSize(16, 16));
	return picture;
}