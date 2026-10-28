#ifndef __SERIALIZE_FILE_PROVIDER_H__
#define __SERIALIZE_FILE_PROVIDER_H__

#include "core/serialize/dataBuilder.h"
#include <functional>

// ⭐ A CONFIGURATION AS A DIRECTORY OF FILES — for a repository and its diffs. A node is a file, written in the
// engine's one JSON form of a node (ibJsonProvider), and named by what it is: `CommonModule/Main/node.json`, its
// children in folders under it, a text of several lines and a binary beside it in a file of its own (`Module.txt`;
// from inside a value — the names on the way, `ObjectModule.Module.txt`). The root is `configuration.json`.
//
// ⭐ LAID OUT IN MEMORY FIRST — `files`, a node holding one field a file: its path and its bytes. The files are written
// where the directory is and the tree is read where the configuration is, and in a thin designer those are two
// machines: the set travels between them as one node (its binary form), and only Publish and Collect touch a disk.
class CORE_API ibFileProvider {
public:
	// clsid <-> name, as ibJsonProvider's: a folder is named by the type (Catalog/Goods), and a node file says its type
	// by name where the name reads back as the same type — by number where it does not, or nothing could read it back.
	void SetTypeResolver(std::function<ibString(ibClassID)> fn) { m_resolveType = std::move(fn); }
	void SetTypeLookup(std::function<ibClassID(const ibString&)> fn) { m_lookupType = std::move(fn); }

	// The tree as files, and back.
	bool Write(const ibDataNode& root, ibDataNode& files) const;
	bool Read(const ibDataNode& files, ibDataNode& root) const;

	// The set on a disk: published into a directory that holds an earlier export or nothing — never somebody's other
	// files — replaced whole or left as it was; and read out of one, the files its export names and no others.
	static bool Publish(const ibDataNode& files, const ibString& directory);
	static bool Collect(const ibString& directory, ibDataNode& files);

private:
	std::function<ibString(ibClassID)>       m_resolveType;
	std::function<ibClassID(const ibString&)> m_lookupType;
};

#endif // __SERIALIZE_FILE_PROVIDER_H__
