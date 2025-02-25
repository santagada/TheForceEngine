#include "modLoader.h"
#include "frontEndUi.h"
#include "console.h"
#include "editorTexture.h"
#include "profilerView.h"
#include <TFE_DarkForces/config.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_System/system.h>
#include <TFE_System/parser.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_Archive/archive.h>
#include <TFE_Settings/settings.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_Archive/zipArchive.h>
#include <TFE_Archive/gobMemoryArchive.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_Ui/ui.h>
#include <TFE_Ui/markdown.h>
#include <TFE_Ui/imGUI/imgui.h>
// Game
#include <TFE_DarkForces/mission.h>
#include <TFE_Jedi/Renderer/jediRenderer.h>

using namespace TFE_Input;

namespace TFE_FrontEndUI
{
	enum QueuedReadType
	{
		QREAD_DIR = 0,
		QREAD_ZIP,
		QREAD_COUNT
	};

	struct QueuedRead
	{
		QueuedReadType type;
		std::string path;
		std::string fileName;
	};

	struct ModData
	{
		std::vector<std::string> gobFiles;
		std::string textFile;
		std::string imageFile;
		EditorTexture image;

		std::string name;
		std::string relativePath;
		std::string text;

		bool invertImage = true;
	};
	static std::vector<ModData> s_mods;
		
	static std::vector<char> s_fileBuffer;
	static std::vector<u8> s_readBuffer[2];
	static s32 s_selectedMod;

	static std::vector<QueuedRead> s_readQueue;
	static std::vector<u8> s_imageBuffer;
	static size_t s_readIndex = 0;

	static ViewMode s_viewMode = VIEW_IMAGES;

	void fixupName(char* name);
	void readFromQueue(size_t itemsPerFrame);
	bool parseNameFromText(const char* textFileName, const char* path, char* name, std::string* fullText);
	void extractPosterFromImage(const char* baseDir, const char* zipFile, const char* imageFileName, EditorTexture* poster);
	void extractPosterFromMod(const char* baseDir, const char* archiveFileName, EditorTexture* poster);

	void modLoader_read()
	{
		s_mods.clear();
		s_selectedMod = -1;
		clearSelectedMod();

		s_readQueue.clear();
		s_readIndex = 0;

		// There are 3 possible mod directory locations:
		// In the TFE directory,
		// In the original source data.
		// In ProgramData/
		char sourceDataModDir[TFE_MAX_PATH];
		snprintf(sourceDataModDir, TFE_MAX_PATH, "%sMods/", TFE_Paths::getPath(PATH_SOURCE_DATA));
		TFE_Paths::fixupPathAsDirectory(sourceDataModDir);

		// Add Mods/ paths to the program data directory and local executable directory.
		// Note only directories that exist are actually added.
		const char* programData = TFE_Paths::getPath(PATH_PROGRAM_DATA);
		const char* programDir = TFE_Paths::getPath(PATH_PROGRAM);

		char programDataModDir[TFE_MAX_PATH];
		sprintf(programDataModDir, "%sMods/", programData);
		TFE_Paths::fixupPathAsDirectory(programDataModDir);

		char programDirModDir[TFE_MAX_PATH];
		sprintf(programDirModDir, "%sMods/", programDir);
		TFE_Paths::fixupPathAsDirectory(programDirModDir);

		s32 modPathCount = 0;
		char modPaths[3][TFE_MAX_PATH];
		if (FileUtil::directoryExits(sourceDataModDir))
		{
			strcpy(modPaths[modPathCount], sourceDataModDir);
			modPathCount++;
		}
		if (FileUtil::directoryExits(programDataModDir))
		{
			strcpy(modPaths[modPathCount], programDataModDir);
			modPathCount++;
		}
		if (FileUtil::directoryExits(programDirModDir))
		{
			strcpy(modPaths[modPathCount], programDirModDir);
			modPathCount++;
		}

		if (!modPathCount)
		{
			return;
		}

		FileList dirList, zipList;
		for (s32 i = 0; i < modPathCount; i++)
		{
			dirList.clear();
			FileUtil::readSubdirectories(modPaths[i], dirList);
			if (dirList.empty()) { continue; }

			const size_t count = dirList.size();
			const std::string* dir = dirList.data();
			for (size_t d = 0; d < count; d++)
			{
				s_readQueue.push_back({ QREAD_DIR, dir[d], "" });
			}
		}
		// Read Zip Files.
		for (s32 i = 0; i < modPathCount; i++)
		{
			zipList.clear();
			FileUtil::readDirectory(modPaths[i], "zip", zipList);
			size_t count = zipList.size();
			for (size_t z = 0; z < count; z++)
			{
				s_readQueue.push_back({ QREAD_ZIP, modPaths[i], zipList[z] });
			}
		}
	}

	void modLoader_cleanupResources()
	{
		for (size_t i = 0; i < s_mods.size(); i++)
		{
			if (s_mods[i].image.texture)
			{
				TFE_RenderBackend::freeTexture(s_mods[i].image.texture);
			}
		}
		s_mods.clear();
	}
		
	ViewMode modLoader_getViewMode()
	{
		return s_viewMode;
	}

	void modLoader_imageListUI(f32 uiScale)
	{
		DisplayInfo dispInfo;
		TFE_RenderBackend::getDisplayInfo(&dispInfo);
		s32 columns = max(1, (dispInfo.width - s32(16*uiScale)) / s32(268*uiScale));

		f32 y = ImGui::GetCursorPosY();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		for (size_t i = 0; i < s_mods.size();)
		{
			for (s32 x = 0; x < columns && i < s_mods.size(); x++, i++)
			{
				char label[32];
				sprintf(label, "###%zd", i);
				ImGui::SetCursorPos(ImVec2((f32(x) * 268.0f + 16.0f)*uiScale, y));
				ImGui::InvisibleButton(label, ImVec2(256*uiScale, 192*uiScale));
				if (ImGui::IsItemClicked() && s_selectedMod < 0)
				{
					s_selectedMod = s32(i);
					TFE_System::logWrite(LOG_MSG, "Mods", "Selected Mod = %d", i);
				}

				f32 yScrolled = y - ImGui::GetScrollY();

				if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				{
					drawList->AddImageRounded(TFE_RenderBackend::getGpuPtr(s_mods[i].image.texture), ImVec2((f32(x) * 268 + 16 - 2)*uiScale, yScrolled - 2*uiScale),
						ImVec2((f32(x) * 268 + 16 + 256 + 2)*uiScale, yScrolled + (192 + 2)*uiScale), ImVec2(0.0f, s_mods[i].invertImage ? 1.0f : 0.0f),
						ImVec2(1.0f, s_mods[i].invertImage ? 0.0f : 1.0f), 0xffffffff, 8.0f, ImDrawCornerFlags_All);
					drawList->AddImageRounded(getGradientTexture(), ImVec2((f32(x) * 268 + 16 - 2)*uiScale, yScrolled - 2*uiScale),
						ImVec2((f32(x) * 268 + 16 + 256 + 2)*uiScale, yScrolled + (192 + 2)*uiScale), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
						0x40ffb080, 8.0f, ImDrawCornerFlags_All);
				}
				else
				{
					drawList->AddImageRounded(TFE_RenderBackend::getGpuPtr(s_mods[i].image.texture), ImVec2((f32(x) * 268 + 16)*uiScale, yScrolled),
						ImVec2((f32(x) * 268 + 16 + 256)*uiScale, yScrolled + 192*uiScale), ImVec2(0.0f, s_mods[i].invertImage ? 1.0f : 0.0f),
						ImVec2(1.0f, s_mods[i].invertImage ? 0.0f : 1.0f), 0xffffffff, 8.0f, ImDrawCornerFlags_All);
				}

				ImGui::SetCursorPos(ImVec2((f32(x) * 268 + 20)*uiScale, y + 192*uiScale));

				// Limit the name to 36 characters to avoid going into the next cell.
				if (s_mods[i].name.length() <= 36)
				{
					ImGui::LabelText("###Label", s_mods[i].name.c_str());
				}
				else
				{
					char name[TFE_MAX_PATH];
					strcpy(name, s_mods[i].name.c_str());
					name[33] = '.';
					name[34] = '.';
					name[35] = '.';
					name[36] = 0;
					ImGui::LabelText("###Label", name);
				}
			}
			y += 232*uiScale;
		}
	}

	void modLoader_NameListUI(f32 uiScale)
	{
		DisplayInfo dispInfo;
		TFE_RenderBackend::getDisplayInfo(&dispInfo);
		s32 rowCount = (dispInfo.height - s32(112*uiScale)) / s32(28*uiScale);

		char buttonLabel[32];
		ImGui::PushFont(getDialogFont());
		size_t i = 0;
		for (s32 x = 0; i < s_mods.size(); x++)
		{
			for (s32 y = 0; y < rowCount && i < s_mods.size(); y++, i++)
			{
				sprintf(buttonLabel, "###mod%zd", i);
				ImVec2 cursor((8.0f + x * 410)*uiScale, (88.0f + y * 28)*uiScale);
				ImGui::SetCursorPos(cursor);
				if (ImGui::Button(buttonLabel, ImVec2(400*uiScale, 24*uiScale)) && s_selectedMod < 0)
				{
					s_selectedMod = s32(i);
					TFE_System::logWrite(LOG_MSG, "Mods", "Selected Mod = %d", i);
				}
				
				ImGui::SetCursorPos(ImVec2(cursor.x + 8.0f*uiScale, cursor.y - 2.0f*uiScale));
				char name[TFE_MAX_PATH];
				strcpy(name, s_mods[i].name.c_str());
				size_t len = strlen(name);
				if (len > 36)
				{
					name[33] = '.';
					name[34] = '.';
					name[35] = '.';
					name[36] = 0;
				}

				ImGui::LabelText("###", name);
			}
		}
		ImGui::PopFont();
	}

	void modLoader_FileListUI(f32 uiScale)
	{
		DisplayInfo dispInfo;
		TFE_RenderBackend::getDisplayInfo(&dispInfo);
		s32 rowCount = (dispInfo.height - s32(112*uiScale)) / s32(28*uiScale);

		char buttonLabel[32];
		ImGui::PushFont(getDialogFont());
		size_t i = 0;
		for (s32 x = 0; i < s_mods.size(); x++)
		{
			for (s32 y = 0; y < rowCount && i < s_mods.size(); y++, i++)
			{
				sprintf(buttonLabel, "###mod%zd", i);
				ImVec2 cursor((8.0f + x * 410)*uiScale, (88.0f + y * 28)*uiScale);
				ImGui::SetCursorPos(cursor);
				if (ImGui::Button(buttonLabel, ImVec2(400*uiScale, 24*uiScale)) && s_selectedMod < 0)
				{
					s_selectedMod = s32(i);
					TFE_System::logWrite(LOG_MSG, "Mods", "Selected Mod = %d", i);
				}

				ImGui::SetCursorPos(ImVec2(cursor.x + 8.0f*uiScale, cursor.y - 2.0f*uiScale));
				char name[TFE_MAX_PATH];
				strcpy(name, s_mods[i].gobFiles[0].c_str());
				size_t len = strlen(name);
				if (len > 36)
				{
					name[33] = '.';
					name[34] = '.';
					name[35] = '.';
					name[36] = 0;
				}

				ImGui::LabelText("###", name);
			}
		}
		ImGui::PopFont();
	}

	void modLoader_selectionUI()
	{
		f32 uiScale = (f32)TFE_Ui::getUiScale() * 0.01f;

		// Load in the mod data a few at a time so to limit waiting for loading.
		readFromQueue(1);
		clearSelectedMod();
		if (s_mods.empty()) { return; }

		ImGui::Separator();
		ImGui::PushFont(getDialogFont());

		ImGui::LabelText("###", "VIEW");
		ImGui::SameLine(128.0f*uiScale);

		bool viewImages   = s_viewMode == VIEW_IMAGES;
		bool viewNameList = s_viewMode == VIEW_NAME_LIST;
		bool viewFileList = s_viewMode == VIEW_FILE_LIST;
		if (ImGui::Checkbox("Images", &viewImages))
		{
			if (viewImages)
			{
				s_viewMode = VIEW_IMAGES;
			}
			else
			{
				s_viewMode = VIEW_NAME_LIST;
			}
		}
		ImGui::SameLine(236.0f*uiScale);
		if (ImGui::Checkbox("Name List", &viewNameList))
		{
			if (viewNameList)
			{
				s_viewMode = VIEW_NAME_LIST;
			}
			else
			{
				s_viewMode = VIEW_FILE_LIST;
			}
		}
		ImGui::SameLine(380.0f*uiScale);
		if (ImGui::Checkbox("File List", &viewFileList))
		{
			if (viewFileList)
			{
				s_viewMode = VIEW_FILE_LIST;
			}
			else
			{
				s_viewMode = VIEW_IMAGES;
			}
		}
		ImGui::PopFont();

		ImGui::Separator();
			   
		if (s_viewMode == VIEW_IMAGES)
		{
			modLoader_imageListUI(uiScale);
		}
		else if (s_viewMode == VIEW_NAME_LIST)
		{
			modLoader_NameListUI(uiScale);
		}
		else if (s_viewMode == VIEW_FILE_LIST)
		{
			modLoader_FileListUI(uiScale);
		}

		if (s_selectedMod >= 0)
		{
			DisplayInfo dispInfo;
			TFE_RenderBackend::getDisplayInfo(&dispInfo);

			bool open = true;
			bool retFromLoader = false;
			s32 infoWidth  = dispInfo.width  - s32(120*uiScale);
			s32 infoHeight = dispInfo.height - s32(120*uiScale);

			const u32 window_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
			ImGui::SetCursorPos(ImVec2(10*uiScale, 10*uiScale));
			ImGui::Begin("Mod Info", &open, ImVec2(f32(infoWidth), f32(infoHeight)), 1.0f, window_flags);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 cursor = ImGui::GetCursorPos();
			drawList->AddImageRounded(TFE_RenderBackend::getGpuPtr(s_mods[s_selectedMod].image.texture), ImVec2(cursor.x + 64, cursor.y + 64), ImVec2(cursor.x + 64 + 320*uiScale, cursor.y + 64 + 200*uiScale),
				ImVec2(0.0f, s_mods[s_selectedMod].invertImage ? 1.0f : 0.0f), ImVec2(1.0f, s_mods[s_selectedMod].invertImage ? 0.0f : 1.0f), 0xffffffff, 8.0f, ImDrawCornerFlags_All);

			ImGui::PushFont(getDialogFont());
			ImGui::SetCursorPosX(cursor.x + (320 + 70)*uiScale);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.9f, 1.0f, 1.0f));
			ImGui::LabelText("###", s_mods[s_selectedMod].name.c_str());
			ImGui::PopStyleColor();

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.75f));
			ImGui::SetCursorPos(ImVec2(cursor.x + 10*uiScale, cursor.y + 220*uiScale));
			ImGui::Text("Game: Dark Forces");
			ImGui::SetCursorPosX(cursor.x + 10*uiScale);
			ImGui::Text("Type: Vanilla Compatible");
			ImGui::SetCursorPosX(cursor.x + 10*uiScale);
			ImGui::Text("File: %s", s_mods[s_selectedMod].gobFiles[0].c_str());
			ImGui::PopStyleColor();

			ImGui::SetCursorPos(ImVec2(cursor.x + 90*uiScale, cursor.y + 320*uiScale));
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
			if (ImGui::Button("PLAY", ImVec2(128*uiScale, 32*uiScale)))
			{
				char selectedModCmd[TFE_MAX_PATH];
				sprintf(selectedModCmd, "-u%s%s", s_mods[s_selectedMod].relativePath.c_str(), s_mods[s_selectedMod].gobFiles[0].c_str());
				setSelectedMod(selectedModCmd);

				setState(APP_STATE_GAME);
				clearMenuState();
				open = false;
				retFromLoader = true;
			}
			ImGui::PopStyleColor(3);

			ImGui::SetCursorPos(ImVec2(cursor.x + 90*uiScale, cursor.y + 360*uiScale));
			if (ImGui::Button("CANCEL", ImVec2(128*uiScale, 32*uiScale)))
			{
				open = false;
			}

			ImGui::PopFont();

			ImGui::SetCursorPos(ImVec2(cursor.x + 328*uiScale, cursor.y + 30*uiScale));
			ImGui::BeginChild("###Mod Info Text", ImVec2(f32(infoWidth - 344*uiScale), f32(infoHeight - 68*uiScale)), true, ImGuiWindowFlags_NoBringToFrontOnFocus);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.75f));
			ImGui::TextWrapped(s_mods[s_selectedMod].text.c_str());
			ImGui::PopStyleColor();
			ImGui::EndChild();

			if (!ImGui::IsRootWindowOrAnyChildFocused())
			{
				open = false;
			}
			ImGui::End();

			if (!open)
			{
				s_selectedMod = -1;
				if (retFromLoader)
				{
					modLoader_cleanupResources();
				}
			}
		}
	}

	void fixupName(char* name)
	{
		size_t len = strlen(name);
		name[0] = toupper(name[0]);
		for (size_t i = 1; i < len; i++)
		{
			name[i] = tolower(name[i]);
		}
	}

	bool parseNameFromText(const char* textFileName, const char* path, char* name, std::string* fullText)
	{
		if (!textFileName || textFileName[0] == 0) { return false; }

		const size_t len = strlen(textFileName);
		const char* ext = &textFileName[len - 3];
		size_t textLen = 0;
		if (strcasecmp(ext, "zip") == 0)
		{
			ZipArchive zipArchive;
			char zipPath[TFE_MAX_PATH];
			sprintf(zipPath, "%s%s", path, textFileName);
			if (zipArchive.open(zipPath))
			{
				s32 txtIndex = -1;
				const u32 count = zipArchive.getFileCount();
				for (u32 i = 0; i < count; i++)
				{
					const char* name = zipArchive.getFileName(i);
					const size_t nameLen = strlen(name);
					const char* zext = &name[nameLen - 3];
					if (strcasecmp(zext, "txt") == 0)
					{
						txtIndex = i;
						break;
					}
				}

				if (txtIndex >= 0 && zipArchive.openFile(txtIndex))
				{
					textLen = zipArchive.getFileLength();
					s_fileBuffer.resize(textLen);
					zipArchive.readFile(s_fileBuffer.data(), textLen);
					zipArchive.closeFile();
				}
			}
		}
		else
		{
			char fullPath[TFE_MAX_PATH];
			sprintf(fullPath, "%s%s", path, textFileName);

			FileStream textFile;
			if (!textFile.open(fullPath, FileStream::MODE_READ))
			{
				return false;
			}
			textLen = textFile.getSize();
			s_fileBuffer.resize(textLen);
			textFile.readBuffer(s_fileBuffer.data(), (u32)textLen);
			textFile.close();
		}
		if (!textLen)
		{
			return false;
		}

		// Some files start with garbage at the beginning...
		// So try a small probe first to see if such fixup is reqiured.
		bool needsFixup = false;
		for (size_t i = 0; i < 10 && i < s_fileBuffer.size(); i++)
		{
			if (s_fileBuffer[i] == 0)
			{
				needsFixup = true;
				break;
			}
		}
		// If it is, try to find a valid start.
		size_t lastZero = 0;
		if (needsFixup)
		{
			size_t len = s_fileBuffer.size();
			const char* text = s_fileBuffer.data();
			for (size_t i = 0; i < len - 1 && i < 128; i++)
			{
				if (text[i] == 0)
				{
					lastZero = i;
				}
			}
			if (lastZero) { lastZero++; }
		}
		*fullText = std::string(s_fileBuffer.data() + lastZero, s_fileBuffer.data() + s_fileBuffer.size());

		TFE_Parser parser;
		parser.init(fullText->c_str(), fullText->length());
		// First pass - look for "Title" followed by ':'
		size_t bufferPos = 0;
		size_t titleLen = strlen("Title");
		bool foundTitle = false;
		while (!foundTitle)
		{
			const char* line = parser.readLine(bufferPos, true);
			if (!line)
			{
				break;
			}

			if (strncasecmp("Title", line, titleLen) == 0)
			{
				size_t lineLen = strlen(line);
				for (size_t c = titleLen + 1; c < lineLen; c++)
				{
					if (line[c] == ':' && line[c + 1] == ' ')
					{
						// Found it.
						strcpy(name, &line[c + 2]);
						foundTitle = true;
						break;
					}
				}
			}
		}

		if (!foundTitle)
		{
			// Looking for "Title" failed, try reading the first 'valid' line.
			bufferPos = 0;
			while (!foundTitle)
			{
				const char* line = parser.readLine(bufferPos, true);
				if (!line)
				{
					break;
				}
				if (line[0] == '<' || line[0] == '>' || line[0] == '=' || line[0] == '|')
				{
					continue;
				}
				if (strncasecmp(line, "PRESENTING", strlen("PRESENTING")) == 0)
				{
					continue;
				}

				strcpy(name, line);
				foundTitle = true;
				break;
			}
		}

		if (foundTitle)
		{
			size_t titleLen = strlen(name);
			size_t lastValid = 0;
			for (size_t c = 0; c < titleLen; c++)
			{
				if (name[c] != ' ' && name[c] != '-')
				{
					lastValid = c;
				}
				if (name[c] == '-')
				{
					break;
				}
			}
			if (lastValid > 0)
			{
				name[lastValid + 1] = 0;
			}
			return true;
		}

		return false;
	}

	void readFromQueue(size_t itemsPerFrame)
	{
		FileList gobFiles, txtFiles, imgFiles;
		const size_t readEnd = min(s_readIndex + itemsPerFrame, s_readQueue.size());
		const QueuedRead* reads = s_readQueue.data();
		for (size_t i = s_readIndex; i < readEnd; i++, s_readIndex++)
		{
			if (reads[i].type == QREAD_DIR)
			{
				// Clear doesn't deallocate in most implementations, so doing it this way should reduce memory allocations.
				gobFiles.clear();
				txtFiles.clear();
				imgFiles.clear();

				const char* subDir = reads[i].path.c_str();
				FileUtil::readDirectory(subDir, "gob", gobFiles);
				FileUtil::readDirectory(subDir, "txt", txtFiles);
				FileUtil::readDirectory(subDir, "jpg", imgFiles);

				// No gob files = no mod.
				if (gobFiles.size() != 1)
				{
					continue;
				}
				s_mods.push_back({});
				ModData& mod = s_mods.back();

				mod.gobFiles = gobFiles;
				mod.textFile = txtFiles.empty() ? "" : txtFiles[0];
				mod.imageFile = imgFiles.empty() ? "" : imgFiles[0];

				size_t fullDirLen = strlen(subDir);
				for (size_t i = 0; i < fullDirLen; i++)
				{
					if (strncasecmp("Mods", &subDir[i], 4) == 0)
					{
						mod.relativePath = &subDir[i + 5];
						break;
					}
				}

				if (mod.imageFile.empty())
				{
					extractPosterFromMod(subDir, mod.gobFiles[0].c_str(), &mod.image);
					mod.invertImage = true;
				}
				else
				{
					extractPosterFromImage(subDir, nullptr, mod.imageFile.c_str(), &mod.image);
					mod.invertImage = false;
				}

				char name[TFE_MAX_PATH];
				if (!parseNameFromText(mod.textFile.c_str(), subDir, name, &mod.text))
				{
					const char* gobFileName = mod.gobFiles[0].c_str();
					memcpy(name, gobFileName, strlen(gobFileName) - 4);
					name[strlen(gobFileName) - 4] = 0;
					fixupName(name);
				}

				mod.name = name;
			}
			else
			{
				ZipArchive zipArchive;
				const char* modPath = reads[i].path.c_str();
				const char* zipName = reads[i].fileName.c_str();

				char zipPath[TFE_MAX_PATH];
				sprintf(zipPath, "%s%s", modPath, zipName);
				if (!zipArchive.open(zipPath)) { continue; }

				s32 gobFileIndex = -1;
				s32 txtFileIndex = -1;
				s32 jpgFileIndex = -1;

				// Look for the following:
				// 1. Gob File.
				// 2. Text File.
				// 3. JPG
				for (u32 f = 0; f < zipArchive.getFileCount(); f++)
				{
					const char* fileName = zipArchive.getFileName(f);
					size_t len = strlen(fileName);
					if (len <= 4)
					{
						continue;
					}
					const char* ext = &fileName[len - 3];
					if (strcasecmp(ext, "gob") == 0)
					{
						gobFileIndex = s32(f);
					}
					else if (strcasecmp(ext, "txt") == 0)
					{
						txtFileIndex = s32(f);
					}
					else if (strcasecmp(ext, "jpg") == 0)
					{
						jpgFileIndex = s32(f);
					}
				}
				if (gobFileIndex >= 0)
				{
					s_mods.push_back({});
					ModData& mod = s_mods.back();
					mod.gobFiles.push_back(zipName);

					char name[TFE_MAX_PATH];
					if (!parseNameFromText(mod.gobFiles[0].c_str(), modPath, name, &mod.text))
					{
						const char* gobFileName = mod.gobFiles[0].c_str();
						memcpy(name, gobFileName, strlen(gobFileName) - 4);
						name[strlen(gobFileName) - 4] = 0;
						fixupName(name);
					}
					mod.name = name;

					if (jpgFileIndex < 0)
					{
						extractPosterFromMod(modPath, mod.gobFiles[0].c_str(), &mod.image);
						mod.invertImage = true;
					}
					else
					{
						extractPosterFromImage(modPath, mod.gobFiles[0].c_str(), zipArchive.getFileName(jpgFileIndex), &mod.image);
						mod.invertImage = false;
					}
				}

				zipArchive.close();
			}
		}
	}

	void extractPosterFromImage(const char* baseDir, const char* zipFile, const char* imageFileName, EditorTexture* poster)
	{
		if (zipFile && zipFile[0])
		{
			char zipPath[TFE_MAX_PATH];
			sprintf(zipPath, "%s%s", baseDir, zipFile);

			ZipArchive zipArchive;
			if (!zipArchive.open(zipPath)) { return; }
			if (zipArchive.openFile(imageFileName))
			{
				size_t imageSize = zipArchive.getFileLength();
				s_imageBuffer.resize(imageSize);
				zipArchive.readFile(s_imageBuffer.data(), imageSize);
				zipArchive.closeFile();

				Image* image = TFE_Image::loadFromMemory((u8*)s_imageBuffer.data(), imageSize);
				if (image)
				{
					TextureGpu* gpuImage = TFE_RenderBackend::createTexture(image->width, image->height, image->data, MAG_FILTER_LINEAR);
					poster->texture = gpuImage;
					poster->width = image->width;
					poster->height = image->height;

					delete[] image->data;
					delete image;
				}
			}
			zipArchive.close();
		}
		else
		{
			char imagePath[TFE_MAX_PATH];
			sprintf(imagePath, "%s%s", baseDir, imageFileName);

			Image* image = TFE_Image::get(imagePath);
			if (image)
			{
				TextureGpu* gpuImage = TFE_RenderBackend::createTexture(image->width, image->height, image->data, MAG_FILTER_LINEAR);
				poster->texture = gpuImage;
				poster->width = image->width;
				poster->height = image->height;
			}
		}
	}

	void extractPosterFromMod(const char* baseDir, const char* archiveFileName, EditorTexture* poster)
	{
		// Extract a "poster", if possible, from the GOB file.
		// And then save it as a JPG in /ProgramData/TheForceEngine/ModPosters/NAME.jpg
		char modPath[TFE_MAX_PATH], srcPath[TFE_MAX_PATH], srcPathTex[TFE_MAX_PATH];
		sprintf(modPath, "%s%s", baseDir, archiveFileName);
		sprintf(srcPath, "%s%s", TFE_Paths::getPath(PATH_SOURCE_DATA), "DARK.GOB");
		sprintf(srcPathTex, "%s%s", TFE_Paths::getPath(PATH_SOURCE_DATA), "TEXTURES.GOB");

		GobMemoryArchive gobMemArchive;
		const size_t len = strlen(archiveFileName);
		const char* archiveExt = &archiveFileName[len - 3];
		Archive* archiveMod = nullptr;
		bool archiveIsGob = true;
		if (strcasecmp(archiveExt, "zip") == 0)
		{
			archiveIsGob = false;

			ZipArchive zipArchive;
			if (zipArchive.open(modPath))
			{
				// Find the gob...
				s32 gobIndex = -1;
				const u32 count = zipArchive.getFileCount();
				for (u32 i = 0; i < count; i++)
				{
					const char* name = zipArchive.getFileName(i);
					const size_t nameLen = strlen(name);
					const char* zext = &name[nameLen - 3];
					if (strcasecmp(zext, "gob") == 0)
					{
						gobIndex = i;
						break;
					}
				}

				if (gobIndex >= 0)
				{
					size_t bufferLen = zipArchive.getFileLength(gobIndex);
					u8* buffer = (u8*)malloc(bufferLen);
					zipArchive.openFile(gobIndex);
					zipArchive.readFile(buffer, bufferLen);
					zipArchive.closeFile();

					gobMemArchive.open(buffer, bufferLen);
					archiveMod = &gobMemArchive;
				}

				zipArchive.close();
			}
		}
		else
		{
			archiveMod = Archive::getArchive(ARCHIVE_GOB, archiveFileName, modPath);
		}
		Archive* archiveTex = Archive::getArchive(ARCHIVE_GOB, "TEXTURES.GOB", srcPathTex);
		Archive* archiveBase = Archive::getArchive(ARCHIVE_GOB, "DARK.GOB", srcPath);

		s_readBuffer[0].clear();
		s_readBuffer[1].clear();
		if (archiveMod || archiveBase)
		{
			if (archiveMod && archiveMod->fileExists("wait.bm") && archiveMod->openFile("wait.bm"))
			{
				s_readBuffer[0].resize(archiveMod->getFileLength());
				archiveMod->readFile(s_readBuffer[0].data(), archiveMod->getFileLength());
				archiveMod->closeFile();
			}
			else if (archiveTex->openFile("wait.bm"))
			{
				s_readBuffer[0].resize(archiveTex->getFileLength());
				archiveTex->readFile(s_readBuffer[0].data(), archiveTex->getFileLength());
				archiveTex->closeFile();
			}

			if (archiveMod && archiveMod->fileExists("wait.pal") && archiveMod->openFile("wait.pal"))
			{
				s_readBuffer[1].resize(archiveMod->getFileLength());
				archiveMod->readFile(s_readBuffer[1].data(), archiveMod->getFileLength());
				archiveMod->closeFile();
			}
			else if (archiveBase->openFile("wait.pal"))
			{
				s_readBuffer[1].resize(archiveBase->getFileLength());
				archiveBase->readFile(s_readBuffer[1].data(), archiveBase->getFileLength());
				archiveBase->closeFile();
			}
		}

		if (!s_readBuffer[0].empty() && !s_readBuffer[1].empty())
		{
			TextureData* imageData = bitmap_loadFromMemory(s_readBuffer[0].data(), s_readBuffer[0].size(), 1);
			u32 palette[256];
			convertPalette(s_readBuffer[1].data(), palette);
			createTexture(imageData, palette, poster, MAG_FILTER_LINEAR);
		}

		if (archiveMod && archiveIsGob)
		{
			Archive::freeArchive(archiveMod);
		}
	}
}