#include <algorithm>
#include <chrono>
#include <string>
#include <regex>

#include "TextEditor.h"

static const int cTextStart = 7;

// TODO
// - multiline comments vs single-line: latter is blocking start of a ML
// - handle non-monospace fonts
// - handle unicode/utf
// - testing

TextEditor::TextEditor()
	: mLineSpacing(0.0f)
	, mUndoIndex(0)
	, mTabSize(4)
	, mOverwrite(false)
	, mWithinRender(false)
	, mScrollToCursor(false)
	, mWordSelectionMode(false)
	, mColorRangeMin(0)
	, mColorRangeMax(0)
	, mCheckMultilineComments(true)
{
	mPalette[(int)TokenType::None] = 0xffffffff;
	mPalette[(int)TokenType::Comment] = 0xff206020;
	mPalette[(int)TokenType::MultiLineComment] = 0xff406020;
	mPalette[(int)TokenType::Number] = 0xff00ff00;
	mPalette[(int)TokenType::Keyword] = 0xffd69c56;  // 569cd6
	mPalette[(int)TokenType::Identifier] = 0xffaaaaaa;
	mPalette[(int)TokenType::KnownIdentifier] = 0xff9bc64d;	// 4dc69b
	mPalette[(int)TokenType::String] = 0xff7070e0;
	mPalette[(int)TokenType::CharLiteral] = 0xff70a0e0;
	mPalette[(int)TokenType::Preprocessor] = 0xff409090;
	mPalette[(int)TokenType::PreprocIdentifier] = 0xffc040a0;
	mPalette[(int)TokenType::Punctuation] = 0xffffffff;

	SetLanguageDefinition(LanguageDefinition::HLSL());
}


TextEditor::~TextEditor()
{
}

void TextEditor::SetLanguageDefinition(const LanguageDefinition & aLanguageDef)
{
	mLanguageDefinition = aLanguageDef;
	mRegexList.clear();

	for (auto& r : mLanguageDefinition.mTokenRegexStrings)
		mRegexList.push_back(std::make_pair(std::regex(r.first, std::regex_constants::optimize), r.second));
}

void TextEditor::SetPalette(const Palette & aValue)
{
	// TODO....
	//mPalette = aValue;
}

int TextEditor::AppendBuffer(std::string& aBuffer, char chr, int aIndex)
{
	if (chr != '\t')
	{
		aBuffer.push_back(chr);
		return aIndex + 1;
	}
	else
	{
		auto num = mTabSize - aIndex % mTabSize;
		for (int j = num; j > 0; --j)
			aBuffer.push_back(' ');
		return aIndex + num;
	}
}

std::string TextEditor::GetText(const Coordinates & aStart, const Coordinates & aEnd) const
{
	std::string result;

	int prevLineNo = aStart.mLine;
	for (auto it = aStart; it <= aEnd; Advance(it))
	{
		if (prevLineNo != it.mLine)
			result.push_back('\n');

		if (it == aEnd)
			break;

		prevLineNo = it.mLine;
		const auto& line = mLines[it.mLine];
		if (!line.empty() && it.mColumn < (int)line.size())
			result.push_back(line[it.mColumn].mChar);
	}

	return result;
}

TextEditor::Coordinates TextEditor::GetActualCursorCoordinates() const
{
	return SanitizeCoordinates(mState.mCursorPosition);
}

TextEditor::Coordinates TextEditor::SanitizeCoordinates(const Coordinates & aValue) const
{
	auto line = std::max(0, std::min((int)mLines.size() - 1, aValue.mLine));
	auto column = mLines.empty() ? 0 : std::min((int)mLines[line].size(), aValue.mColumn);
	return Coordinates(line, column);
}

void TextEditor::Advance(Coordinates & aCoordinates) const
{
	if (aCoordinates.mLine < (int)mLines.size())
	{
		auto& line = mLines[aCoordinates.mLine];

		if (aCoordinates.mColumn + 1 < (int)line.size())
			++aCoordinates.mColumn;
		else
		{
			++aCoordinates.mLine;
			aCoordinates.mColumn = 0;
		}
	}
}

void TextEditor::DeleteRange(const Coordinates & aStart, const Coordinates & aEnd)
{
	assert(aEnd >= aStart);

	if (aEnd == aStart)
		return;

	if (aStart.mLine == aEnd.mLine)
	{
		auto& line = mLines[aStart.mLine];
		if (aEnd.mColumn >= (int)line.size())
			line.erase(line.begin() + aStart.mColumn, line.end());
		else
			line.erase(line.begin() + aStart.mColumn, line.begin() + aEnd.mColumn);
	}
	else
	{
		auto& firstLine = mLines[aStart.mLine];
		auto& lastLine = mLines[aEnd.mLine];

		firstLine.erase(firstLine.begin() + aStart.mColumn, firstLine.end());
		lastLine.erase(lastLine.begin(), lastLine.begin() + aEnd.mColumn);

		if (aStart.mLine < aEnd.mLine)
			firstLine.insert(firstLine.end(), lastLine.begin(), lastLine.end());

		if (aStart.mLine < aEnd.mLine)
			RemoveLine(aStart.mLine + 1, aEnd.mLine + 1);
	}
}

int TextEditor::InsertTextAt(Coordinates& /* inout */ aWhere, const char * aValue)
{
	int totalLines = 0;
	auto chr = *aValue;
	while (chr != '\0')
	{
		if (mLines.empty())
			mLines.push_back(Line());

		auto& line = mLines[aWhere.mLine];

		if (chr == '\r')
		{
			// skip
		}
		else if (chr == '\n')
		{
			if (aWhere.mColumn < (int)line.size())
			{
				auto& newLine = InsertLine(aWhere.mLine + 1);
				newLine.insert(newLine.begin(), line.begin() + aWhere.mColumn, line.end());
				line.erase(line.begin() + aWhere.mColumn, line.end());
			}
			else
			{
				InsertLine(aWhere.mLine + 1);
			}
			++aWhere.mLine;
			aWhere.mColumn = 0;
			++totalLines;
		}
		else
		{
			line.insert(line.begin() + aWhere.mColumn, Glyph(chr, TokenType::None));
			++aWhere.mColumn;
		}
		chr = *(++aValue);
	}

	return totalLines;
}

void TextEditor::AddUndo(UndoRecord& aValue)
{
	mUndoBuffer.resize(mUndoIndex + 1);
	mUndoBuffer.back() = aValue;
	++mUndoIndex;
}

TextEditor::Coordinates TextEditor::ScreenPosToCoordinates(const ImVec2& aPosition) const
{
	ImVec2 origin = ImGui::GetCursorScreenPos();
	ImVec2 local(aPosition.x - origin.x, aPosition.y - origin.y);

	int lineNo = std::max(0, (int)floor(local.y / mCharAdvance.y));
	int columnCoord = std::max(0, (int)floor(local.x / mCharAdvance.x) - cTextStart);

	int column = 0;
	if (lineNo >= 0 && lineNo < (int)mLines.size())
	{
		auto& line = mLines[lineNo];
		auto distance = 0;
		while (distance < columnCoord && column < (int)line.size())
		{
			if (line[column].mChar == '\t')
				distance = (distance / mTabSize) * mTabSize + mTabSize;
			else
				++distance;
			++column;
		}
	}
	return Coordinates(lineNo, column);
}

TextEditor::Coordinates TextEditor::FindWordStart(const Coordinates & aFrom)
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	auto& line = mLines[at.mLine];

	if (at.mColumn >= (int)line.size())
		return at;

	auto cstart = (TokenType)line[at.mColumn].mColorIndex;
	while (at.mColumn > 0)
	{
		if (cstart != (TokenType)line[at.mColumn - 1].mColorIndex)
			break;
		--at.mColumn;
	}
	return at;
}

TextEditor::Coordinates TextEditor::FindWordEnd(const Coordinates & aFrom)
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	auto& line = mLines[at.mLine];

	if (at.mColumn >= (int)line.size())
		return at;

	auto cstart = (TokenType)line[at.mColumn].mColorIndex;
	while (at.mColumn < (int)line.size())
	{
		if (cstart != (TokenType)line[at.mColumn].mColorIndex)
			break;
		++at.mColumn;
	}
	return at;
}

bool TextEditor::IsOnWordBoundary(const Coordinates & aAt)
{
	if (aAt.mLine >= (int)mLines.size() || aAt.mColumn == 0)
		return true;

	auto& line = mLines[aAt.mLine];
	if (aAt.mColumn >= (int)line.size())
		return true;

	return line[aAt.mColumn].mColorIndex != line[aAt.mColumn - 1].mColorIndex;
}

void TextEditor::RemoveLine(int aStart, int aEnd)
{
	ErrorMarkers etmp;
	for (auto& i : mErrorMarkers)
	{
		ErrorMarkers::value_type e(i.first >= aStart ? i.first - 1 : i.first, i.second);
		if (e.first >= aStart && e.first <= aEnd)
			continue;
		etmp.insert(e);
	}
	mErrorMarkers = std::move(etmp);

	Breakpoints btmp;
	for (auto i : mBreakpoints)
	{
		if (i >= aStart && i <= aEnd)
			continue;
		btmp.insert(i >= aStart ? i - 1 : i);
	}
	mBreakpoints = std::move(btmp);

	mLines.erase(mLines.begin() + aStart, mLines.begin() + aEnd);
}

void TextEditor::RemoveLine(int aIndex)
{
	ErrorMarkers etmp;
	for (auto& i : mErrorMarkers)
	{
		ErrorMarkers::value_type e(i.first >= aIndex ? i.first - 1 : i.first, i.second);
		if (e.first == aIndex)
			continue;
		etmp.insert(e);
	}
	mErrorMarkers = std::move(etmp);

	Breakpoints btmp;
	for (auto i : mBreakpoints)
	{
		if (i == aIndex)
			continue;
		btmp.insert(i >= aIndex ? i - 1 : i);
	}
	mBreakpoints = std::move(btmp);

	mLines.erase(mLines.begin() + aIndex);
}

TextEditor::Line& TextEditor::InsertLine(int aIndex)
{
	auto& result = *mLines.insert(mLines.begin() + aIndex, Line());

	ErrorMarkers etmp;
	for (auto& i : mErrorMarkers)
		etmp.insert(ErrorMarkers::value_type(i.first >= aIndex ? i.first + 1 : i.first, i.second));
	mErrorMarkers = std::move(etmp);

	Breakpoints btmp;
	for (auto i : mBreakpoints)
		btmp.insert(i >= aIndex ? i + 1 : i);
	mBreakpoints = std::move(btmp);

	return result;
}

void TextEditor::Render(const char* aTitle, const ImVec2& aSize, bool aBorder)
{
	mWithinRender = true;

	ImGuiIO& io = ImGui::GetIO();
	auto xadv = (io.Fonts->Fonts[0]->IndexXAdvance['X']);
	mCharAdvance = ImVec2(xadv, io.Fonts->Fonts[0]->FontSize + mLineSpacing);

	ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::BeginChild(aTitle, aSize, aBorder, ImGuiWindowFlags_HorizontalScrollbar);

	ImGui::PushAllowKeyboardFocus(true);

	auto shift = io.KeyShift;
	auto ctrl = io.KeyCtrl;
	auto alt = io.KeyAlt;

	if (ImGui::IsWindowFocused())
	{
		if (ImGui::IsWindowHovered())
			ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
		//ImGui::CaptureKeyboardFromApp(true);

		if (ImGui::IsKeyPressed('Z'))
			if (ctrl && !shift && !alt)
				Undo();
		if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace)))
			if (!ctrl && !shift && alt)
				Undo();
		if (ctrl && !shift && !alt && ImGui::IsKeyPressed('Y'))
			Redo();

		if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow)))
			MoveUp(1, shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow)))
			MoveDown(1, shift);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftArrow)))
			MoveLeft(1, shift, ctrl);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightArrow)))
			MoveRight(1, shift, ctrl);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageUp)))
			MoveUp(GetPageSize() - 4, shift);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageDown)))
			MoveDown(GetPageSize() - 4, shift);
		else if (!alt && ctrl && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Home)))
			MoveTop(shift);
		else if (ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_End)))
			MoveBottom(shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Home)))
			MoveHome(shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_End)))
			MoveEnd(shift);
		else if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)))
			Delete();
		else if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace)))
			BackSpace();
		else if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(45))
			mOverwrite ^= true;
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(45))
			Copy();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed('C'))
			Copy();
		else if (!ctrl && shift && !alt && ImGui::IsKeyPressed(45))
			Paste();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed('V'))
			Paste();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed('X'))
			Cut();
		else if (!ctrl && shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)))
			Cut();

		for (size_t i = 0; i < sizeof(io.InputCharacters) / sizeof(io.InputCharacters[0]); i++)
		{
			auto c = (char) io.InputCharacters[i];
			if (c != 0)
			{
				if (isprint(c) || isspace(c))
				{
					if (c == '\r')
						c = '\n';
					EnterCharacter(c);
				}
			}
		}
	}

	if (ImGui::IsWindowHovered())
	{
		if (!shift && !alt)
		{
			if (ImGui::IsMouseClicked(0))
			{
				mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = SanitizeCoordinates(ScreenPosToCoordinates(ImGui::GetMousePos()));
				if (ctrl)
					mWordSelectionMode = true;
				SetSelection(mInteractiveStart, mInteractiveEnd, mWordSelectionMode);
			}
			if (ImGui::IsMouseDoubleClicked(0) && !ctrl)
			{
				mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = SanitizeCoordinates(ScreenPosToCoordinates(ImGui::GetMousePos()));
				mWordSelectionMode = true;
				SetSelection(mInteractiveStart, mInteractiveEnd, mWordSelectionMode);
			}
			else if (ImGui::IsMouseDragging(0) && ImGui::IsMouseDown(0))
			{
				mState.mCursorPosition = mInteractiveEnd = SanitizeCoordinates(ScreenPosToCoordinates(ImGui::GetMousePos()));
				SetSelection(mInteractiveStart, mInteractiveEnd, mWordSelectionMode);
			}
		}

		if (!ImGui::IsMouseDown(0))
			mWordSelectionMode = false;
	}

	ColorizeInternal();

	static std::string buffer;
	auto contentSize = ImGui::GetWindowContentRegionMax();
	auto drawList = ImGui::GetWindowDrawList();
	int appendIndex = 0;
	int longest = cTextStart;

	ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
	auto scrollX = ImGui::GetScrollX();
	auto scrollY = ImGui::GetScrollY();

	auto lineNo = (int)floor(scrollY / mCharAdvance.y);
	auto lineMax = std::max(0, std::min((int)mLines.size() - 1, lineNo + (int)floor((scrollY + contentSize.y) / mCharAdvance.y)));
	if (!mLines.empty())
	{
		while (lineNo <= lineMax)
		{
			ImVec2 lineStartScreenPos = ImVec2(cursorScreenPos.x, cursorScreenPos.y + lineNo * mCharAdvance.y);
			ImVec2 textScreenPos = ImVec2(lineStartScreenPos.x + mCharAdvance.x * cTextStart, lineStartScreenPos.y);

			auto& line = mLines[lineNo];
			longest = std::max(cTextStart + TextDistanceToLineStart(Coordinates(lineNo, line.size())), longest);
			auto columnNo = 0;
			Coordinates lineStartCoord(lineNo, 0);
			Coordinates lineEndCoord(lineNo, line.size());

			int sstart = -1;
			int ssend = -1;

			assert(mState.mSelectionStart <= mState.mSelectionEnd);
			if (mState.mSelectionStart <= lineEndCoord)
				sstart = mState.mSelectionStart > lineStartCoord ? TextDistanceToLineStart(mState.mSelectionStart) : 0;
			if (mState.mSelectionEnd > lineStartCoord)
				ssend = TextDistanceToLineStart(mState.mSelectionEnd < lineEndCoord ? mState.mSelectionEnd : lineEndCoord);

			if (mState.mSelectionEnd.mLine > lineNo)
				++ssend;

			if (sstart != -1 && ssend != -1 && sstart < ssend)
			{
				ImVec2 vstart(lineStartScreenPos.x + (mCharAdvance.x) * (sstart + cTextStart), lineStartScreenPos.y);
				ImVec2 vend(lineStartScreenPos.x + (mCharAdvance.x) * (ssend + cTextStart), lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(vstart, vend, 0x80a06020);
			}

			static char buf[16];

			if (mBreakpoints.find(lineNo + 1) != mBreakpoints.end())
			{
				auto end = ImVec2(lineStartScreenPos.x + contentSize.x, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(lineStartScreenPos, end, 0x40f08000);
			}

			auto errorIt = mErrorMarkers.find(lineNo + 1);
			if (errorIt != mErrorMarkers.end())
			{
				auto end = ImVec2(lineStartScreenPos.x + contentSize.x, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(lineStartScreenPos, end, 0x800020ff);

				if (ImGui::IsMouseHoveringRect(lineStartScreenPos, end))
				{
					ImGui::BeginTooltip();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
					ImGui::Text("Error at line %d:", errorIt->first);
					ImGui::PopStyleColor();
					ImGui::Separator();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.2f, 1.0f));
					ImGui::Text("%s", errorIt->second.c_str());
					ImGui::PopStyleColor();
					ImGui::EndTooltip();
				}
			}

			snprintf(buf, 16, "%6d", lineNo + 1);
			drawList->AddText(ImVec2(lineStartScreenPos.x /*+ mCharAdvance.x * 1*/, lineStartScreenPos.y), 0xff707000, buf);

			if (mState.mCursorPosition.mLine == lineNo)
			{
				auto focused = ImGui::IsWindowFocused();

				if (!HasSelection())
				{
					auto start = ImVec2(lineStartScreenPos.x + scrollX, lineStartScreenPos.y);
					auto end = ImVec2(start.x + contentSize.x + scrollX, start.y + mCharAdvance.y);
					drawList->AddRectFilled(start, end, focused ? 0x40000000 : 0x40808080);
					drawList->AddRect(start, end, 0x40a0a0a0, 1.0f);
				}

				int cx = TextDistanceToLineStart(mState.mCursorPosition);

				if (focused)
				{
					static auto timeStart = std::chrono::system_clock::now();
					auto timeEnd = std::chrono::system_clock::now();
					auto diff = timeEnd - timeStart;
					auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
					if (elapsed > 400)
					{
						ImVec2 cstart(lineStartScreenPos.x + mCharAdvance.x * (cx + cTextStart), lineStartScreenPos.y);
						ImVec2 cend(lineStartScreenPos.x + mCharAdvance.x * (cx + cTextStart) + (mOverwrite ? mCharAdvance.x : 1.0f), lineStartScreenPos.y + mCharAdvance.y);
						drawList->AddRectFilled(cstart, cend, 0xffe0e0e0);
						if (elapsed > 800)
							timeStart = timeEnd;
					}
				}
			}

			appendIndex = 0;
			auto prevColor = line.empty() ? TokenType::None : (line[0].mMultiLineComment ? TokenType::MultiLineComment : line[0].mColorIndex);

			for (auto& glyph : line)
			{
				auto color = glyph.mMultiLineComment ? TokenType::MultiLineComment : glyph.mColorIndex;

				if (color != prevColor && !buffer.empty())
				{
					drawList->AddText(textScreenPos, mPalette[(uint8_t)prevColor], buffer.c_str());
					textScreenPos.x += mCharAdvance.x * buffer.length();
					buffer.clear();
					prevColor = color;
				}
				appendIndex = AppendBuffer(buffer, glyph.mChar, appendIndex);
				++columnNo;
			}

			if (!buffer.empty())
			{
				drawList->AddText(textScreenPos, mPalette[(uint8_t)prevColor], buffer.c_str());
				buffer.clear();
			}
			appendIndex = 0;
			lineStartScreenPos.y += mCharAdvance.y;
			textScreenPos.x = lineStartScreenPos.x + mCharAdvance.x * cTextStart;
			textScreenPos.y = lineStartScreenPos.y;
			++lineNo;
		}
	}


	ImGui::Dummy(ImVec2((longest + 2) * mCharAdvance.x, mLines.size() * mCharAdvance.y));

	if (mScrollToCursor)
	{
		EnsureCursorVisible();
		ImGui::SetWindowFocus();
		mScrollToCursor = false;
	}

	ImGui::PopAllowKeyboardFocus();
	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

	mWithinRender = false;
}

void TextEditor::SetText(const std::string & aText)
{
	mLines.clear();
	for (auto chr : aText)
	{
		if (mLines.empty())
			mLines.push_back(Line());
		if (chr == '\n')
			mLines.push_back(Line());
		else
		{
			mLines.back().push_back(Glyph(chr, TokenType::None));
		}
	}

	mUndoBuffer.clear();

	Colorize();
}

void TextEditor::EnterCharacter(Char aChar)
{
	UndoRecord u;

	u.mBefore = mState;

	if (HasSelection())
	{
		u.mRemoved = GetSelectedText();
		u.mRemovedStart = mState.mSelectionStart;
		u.mRemovedEnd = mState.mSelectionEnd;
		DeleteSelection();
	}

	auto coord = GetActualCursorCoordinates();
	u.mAddedStart = coord;

	if (mLines.empty())
		mLines.push_back(Line());

	auto& line = mLines[coord.mLine];

	if (aChar == '\n')
	{
		InsertLine(coord.mLine + 1);
		auto& newLine = mLines[coord.mLine + 1];
		newLine.insert(newLine.begin(), line.begin() + coord.mColumn, line.end());
		line.erase(line.begin() + coord.mColumn, line.begin() + line.size());
		mState.mCursorPosition = Coordinates(coord.mLine + 1, 0);
	}
	else
	{
		if (mOverwrite && (int)line.size() < coord.mColumn)
			line[coord.mColumn] = Glyph(aChar, TokenType::None);
		else
			line.insert(line.begin() + coord.mColumn, Glyph(aChar, TokenType::None));
		mState.mCursorPosition = coord;
		++mState.mCursorPosition.mColumn;
	}

	u.mAdded = aChar;
	u.mAddedEnd = GetActualCursorCoordinates();
	u.mAfter = mState;

	AddUndo(u);

	Colorize(coord.mLine - 1, 3);
	EnsureCursorVisible();
}

void TextEditor::SetCursorPosition(const Coordinates & aPosition)
{
	if (mState.mCursorPosition != aPosition)
	{
		mState.mCursorPosition = aPosition;
		EnsureCursorVisible();
	}
}

void TextEditor::SetSelectionStart(const Coordinates & aPosition)
{
	mState.mSelectionStart = SanitizeCoordinates(aPosition);
	if (mState.mSelectionStart > mState.mSelectionEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);
}

void TextEditor::SetSelectionEnd(const Coordinates & aPosition)
{
	mState.mSelectionEnd = SanitizeCoordinates(aPosition);
	if (mState.mSelectionStart > mState.mSelectionEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);
}

void TextEditor::SetSelection(const Coordinates & aStart, const Coordinates & aEnd, bool aWordMode)
{
	mState.mSelectionStart = SanitizeCoordinates(aStart);
	mState.mSelectionEnd = SanitizeCoordinates(aEnd);
	if (aStart > aEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);

	if (aWordMode)
	{
		mState.mSelectionStart = FindWordStart(mState.mSelectionStart);
		if (!IsOnWordBoundary(mState.mSelectionEnd))
			mState.mSelectionEnd = FindWordEnd(FindWordStart(mState.mSelectionEnd));
	}
}

void TextEditor::InsertText(const std::string & aValue)
{
	InsertText(aValue.c_str());
}

void TextEditor::InsertText(const char * aValue)
{
	if (aValue == nullptr)
		return;

	auto pos = GetActualCursorCoordinates();
	auto start = std::min(pos, mState.mSelectionStart);
	int totalLines = pos.mLine - start.mLine;

	totalLines += InsertTextAt(pos, aValue);

	SetSelection(pos, pos);
	SetCursorPosition(pos);
	Colorize(start.mLine - 1, totalLines + 2);
}

void TextEditor::DeleteSelection()
{
	assert(mState.mSelectionEnd >= mState.mSelectionStart);

	if (mState.mSelectionEnd == mState.mSelectionStart)
		return;

	DeleteRange(mState.mSelectionStart, mState.mSelectionEnd);

	SetSelection(mState.mSelectionStart, mState.mSelectionStart);
	SetCursorPosition(mState.mSelectionStart);
	Colorize(mState.mSelectionStart.mLine, 1);
}

void TextEditor::MoveUp(int aAmount, bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition.mLine = std::max(0, mState.mCursorPosition.mLine - aAmount);
	if (oldPos != mState.mCursorPosition)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else
			{
				mInteractiveStart = mState.mCursorPosition;
				mInteractiveEnd = oldPos;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);

		EnsureCursorVisible();
	}
}

void TextEditor::MoveDown(int aAmount, bool aSelect)
{
	assert(mState.mCursorPosition.mColumn >= 0);
	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition.mLine = std::max(0, std::min((int)mLines.size() - 1, mState.mCursorPosition.mLine + aAmount));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else
			{
				mInteractiveStart = oldPos;
				mInteractiveEnd = mState.mCursorPosition;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);

		EnsureCursorVisible();
	}
}

void TextEditor::MoveLeft(int aAmount, bool aSelect, bool aWordMode)
{
	if (mLines.empty())
		return;

	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition = GetActualCursorCoordinates();

	while (aAmount-- > 0)
	{
		if (mState.mCursorPosition.mColumn == 0)
		{
			if (mState.mCursorPosition.mLine > 0)
			{
				--mState.mCursorPosition.mLine;
				mState.mCursorPosition.mColumn = mLines[mState.mCursorPosition.mLine].size();
			}
		}
		else
		{
			mState.mCursorPosition.mColumn = std::max(0, mState.mCursorPosition.mColumn - 1);
			if (aWordMode)
				mState.mCursorPosition = FindWordStart(mState.mCursorPosition);
		}
	}

	assert(mState.mCursorPosition.mColumn >= 0);
	if (aSelect)
	{
		if (oldPos == mInteractiveStart)
			mInteractiveStart = mState.mCursorPosition;
		else if (oldPos == mInteractiveEnd)
			mInteractiveEnd = mState.mCursorPosition;
		else
		{
			mInteractiveStart = mState.mCursorPosition;
			mInteractiveEnd = oldPos;
		}
	}
	else
		mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
	SetSelection(mInteractiveStart, mInteractiveEnd, aSelect && aWordMode);

	EnsureCursorVisible();
}

void TextEditor::MoveRight(int aAmount, bool aSelect, bool aWordMode)
{
	auto oldPos = mState.mCursorPosition;

	if (mLines.empty())
		return;

	while (aAmount-- > 0)
	{
		auto& line = mLines[mState.mCursorPosition.mLine];
		if (mState.mCursorPosition.mColumn >= (int)line.size())
		{
			mState.mCursorPosition.mLine = std::max(0, std::min((int)mLines.size() - 1, mState.mCursorPosition.mLine + 1));
			mState.mCursorPosition.mColumn = 0;
		}
		else
		{
			mState.mCursorPosition.mColumn = std::max(0, std::min((int)line.size(), mState.mCursorPosition.mColumn + 1));
			if (aWordMode)
				mState.mCursorPosition = FindWordEnd(mState.mCursorPosition);
		}
	}

	if (aSelect)
	{
		if (oldPos == mInteractiveEnd)
			mInteractiveEnd = SanitizeCoordinates(mState.mCursorPosition);
		else if (oldPos == mInteractiveStart)
			mInteractiveStart = mState.mCursorPosition;
		else
		{
			mInteractiveStart = oldPos;
			mInteractiveEnd = mState.mCursorPosition;
		}
	}
	else
		mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
	SetSelection(mInteractiveStart, mInteractiveEnd, aSelect && aWordMode);

	EnsureCursorVisible();
}

void TextEditor::MoveTop(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(0, 0));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			mInteractiveEnd = oldPos;
			mInteractiveStart = mState.mCursorPosition;
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TextEditor::TextEditor::MoveBottom(bool aSelect)
{
	auto oldPos = GetCursorPosition();
	auto newPos = Coordinates(mLines.size() - 1, 0);
	SetCursorPosition(newPos);
	if (aSelect)
	{
		mInteractiveStart = oldPos;
		mInteractiveEnd = newPos;
	}
	else
		mInteractiveStart = mInteractiveEnd = newPos;
	SetSelection(mInteractiveStart, mInteractiveEnd);
}

void TextEditor::MoveHome(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(mState.mCursorPosition.mLine, 0));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else
			{
				mInteractiveStart = mState.mCursorPosition;
				mInteractiveEnd = oldPos;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TextEditor::MoveEnd(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(mState.mCursorPosition.mLine, mLines[oldPos.mLine].size()));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else
			{
				mInteractiveStart = oldPos;
				mInteractiveEnd = mState.mCursorPosition;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TextEditor::Delete()
{
	if (mLines.empty())
		return;

	UndoRecord u;
	u.mBefore = mState;

	if (HasSelection())
	{
		u.mRemoved = GetSelectedText();
		u.mRemovedStart = mState.mSelectionStart;
		u.mRemovedEnd = mState.mSelectionEnd;

		DeleteSelection();
	}
	else
	{
		auto pos = GetActualCursorCoordinates();
		SetCursorPosition(pos);
		auto& line = mLines[pos.mLine];

		if (pos.mColumn == (int)line.size())
		{
			if (pos.mLine == (int)mLines.size() - 1)
				return;

			u.mRemoved = '\n';
			u.mRemovedStart = u.mRemovedEnd = GetActualCursorCoordinates();
			Advance(u.mRemovedEnd);

			auto& nextLine = mLines[pos.mLine + 1];
			line.insert(line.end(), nextLine.begin(), nextLine.end());
			RemoveLine(pos.mLine + 1);
		}
		else
		{
			u.mRemoved = line[pos.mColumn].mChar;
			u.mRemovedStart = u.mRemovedEnd = GetActualCursorCoordinates();
			u.mRemovedEnd.mColumn++;

			line.erase(line.begin() + pos.mColumn);
		}

		Colorize(pos.mLine, 1);
	}

	u.mAfter = mState;
	AddUndo(u);
}

void TextEditor::BackSpace()
{
	if (mLines.empty())
		return;

	if (HasSelection())
		DeleteSelection();
	else
	{
		mState.mCursorPosition = GetActualCursorCoordinates();
		if (mState.mCursorPosition.mColumn == 0)
		{
			if (mState.mCursorPosition.mLine == 0)
				return;

			auto& line = mLines[mState.mCursorPosition.mLine];
			auto& prevLine = mLines[mState.mCursorPosition.mLine - 1];
			auto prevSize = prevLine.size();
			prevLine.insert(prevLine.end(), line.begin(), line.end());
			RemoveLine(mState.mCursorPosition.mLine);
			--mState.mCursorPosition.mLine;
			mState.mCursorPosition.mColumn = prevSize;
		}
		else
		{
			auto& line = mLines[mState.mCursorPosition.mLine];
			--mState.mCursorPosition.mColumn;
			if (mState.mCursorPosition.mColumn < (int)line.size())
				line.erase(line.begin() + mState.mCursorPosition.mColumn);
		}
		EnsureCursorVisible();
		Colorize(mState.mCursorPosition.mLine, 1);
	}
}

void TextEditor::SelectWordUnderCursor()
{
	auto c = GetCursorPosition();
	SetSelection(FindWordStart(c), FindWordEnd(c));
}

bool TextEditor::HasSelection() const
{
	return mState.mSelectionEnd > mState.mSelectionStart;
}

void TextEditor::Copy()
{
	if (HasSelection())
	{
		ImGui::SetClipboardText(GetSelectedText().c_str());
	}
	else
	{
		if (!mLines.empty())
		{
			std::string str;
			auto& line = mLines[GetActualCursorCoordinates().mLine];
			for (auto& g : line)
				str.push_back(g.mChar);
			ImGui::SetClipboardText(str.c_str());
		}
	}
}

void TextEditor::Cut()
{
	if (HasSelection())
	{
		UndoRecord u;
		u.mBefore = mState;
		u.mRemoved = GetSelectedText();
		u.mRemovedStart = mState.mSelectionStart;
		u.mRemovedEnd = mState.mSelectionEnd;

		Copy();
		DeleteSelection();

		u.mAfter = mState;
		AddUndo(u);
	}
}

void TextEditor::Paste()
{
	auto clipText = ImGui::GetClipboardText();
	if (clipText != nullptr && strlen(clipText) > 0)
	{
		UndoRecord u;
		u.mBefore = mState;

		if (HasSelection())
		{
			u.mRemoved = GetSelectedText();
			u.mRemovedStart = mState.mSelectionStart;
			u.mRemovedEnd = mState.mSelectionEnd;
			DeleteSelection();
		}

		u.mAdded = clipText;
		u.mAddedStart = GetActualCursorCoordinates();

		InsertText(clipText);

		u.mAddedEnd = GetActualCursorCoordinates();
		u.mAfter = mState;
		AddUndo(u);
	}
}

bool TextEditor::CanUndo() const
{
	return mUndoIndex > 0;
}

bool TextEditor::CanRedo() const
{
	return mUndoIndex < (int)mUndoBuffer.size();
}

void TextEditor::Undo(int aSteps)
{
	while (CanUndo() && aSteps-- > 0)
		mUndoBuffer[--mUndoIndex].Undo(this);
}

void TextEditor::Redo(int aSteps)
{
	while (CanRedo() && aSteps-- > 0)
		mUndoBuffer[mUndoIndex++].Redo(this);
}

std::string TextEditor::GetText() const
{
	return GetText(Coordinates(), Coordinates(mLines.size(), 0));
}

std::string TextEditor::GetSelectedText() const
{
	return GetText(mState.mSelectionStart, mState.mSelectionEnd);
}

void TextEditor::ProcessInputs()
{
}

void TextEditor::RenderInternal()
{
}

void TextEditor::Colorize(int aFromLine, int aLines)
{
	int toLine = aLines == -1 ? mLines.size() : std::min((int)mLines.size(), aFromLine + aLines);
	mColorRangeMin = std::min(mColorRangeMin, aFromLine);
	mColorRangeMax = std::max(mColorRangeMax, toLine);
	mColorRangeMin = std::max(0, mColorRangeMin);
	mColorRangeMax = std::max(mColorRangeMin, mColorRangeMax);
	mCheckMultilineComments = true;
}

void TextEditor::ColorizeRange(int aFromLine, int aToLine)
{
	if (mLines.empty() || aFromLine >= aToLine)
		return;

	std::string buffer;
	int endLine = std::max(0, std::min((int)mLines.size(), aToLine));
	for (int i = aFromLine; i < endLine; ++i)
	{
		bool preproc = false;
		auto& line = mLines[i];
		buffer.clear();
		for (auto g : mLines[i])
		{
			buffer.push_back(g.mChar);
			g.mColorIndex = TokenType::None;
		}

		std::match_results<std::string::const_iterator> results;
		auto last = buffer.cend();
		for (auto first = buffer.cbegin(); first != last; ++first)
		{
			for (auto& p : mRegexList)
			{
				if (std::regex_search<std::string::const_iterator>(first, last, results, p.first, std::regex_constants::match_continuous))
				{
					auto v = *results.begin();
					auto start = v.first - buffer.begin();
					auto end = v.second - buffer.begin();
					auto id = buffer.substr(start, end - start);
					auto color = p.second;
					if (color == TokenType::Identifier)
					{
						if (!mLanguageDefinition.mCaseSensitive)
							std::transform(id.begin(), id.end(), id.begin(), ::toupper);

						if (!preproc)
						{
							if (mLanguageDefinition.mKeywords.find(id) != mLanguageDefinition.mKeywords.end())
								color = TokenType::Keyword;
							else if (mLanguageDefinition.mIdentifiers.find(id) != mLanguageDefinition.mIdentifiers.end())
								color = TokenType::KnownIdentifier;
							else if (mLanguageDefinition.mPreprocIdentifiers.find(id) != mLanguageDefinition.mPreprocIdentifiers.end())
								color = TokenType::PreprocIdentifier;
						}
						else
						{
							if (mLanguageDefinition.mPreprocIdentifiers.find(id) != mLanguageDefinition.mPreprocIdentifiers.end())
								color = TokenType::PreprocIdentifier;
							else
								color = TokenType::Identifier;
						}
					}
					else if (color == TokenType::Preprocessor)
					{
						preproc = true;
					}
					for (int j = start; j < end; ++j)
						line[j].mColorIndex = color;
					first += end - start - 1;
					break;
				}
			}
		}
	}
}

void TextEditor::ColorizeInternal()
{
	if (mLines.empty())
		return;
	
	if (mCheckMultilineComments)
	{
		auto end = Coordinates((int)mLines.size(), 0);
		auto commentStart = end;
		auto withinString = false;
		for (auto i = Coordinates(0, 0); i < end; Advance(i))
		{
			auto& line = mLines[i.mLine];
			if (!line.empty())
			{
				auto g = line[i.mColumn];
				auto c = g.mChar;

				bool inComment = commentStart <= i;

				if (withinString)
				{
					line[i.mColumn].mMultiLineComment = inComment;

					if (c == '\"')
					{
						if (i.mColumn + 1 < (int)line.size() && line[i.mColumn + 1].mChar == '\"')
						{
							Advance(i);
							if (i.mColumn < (int)line.size())
								line[i.mColumn].mMultiLineComment = inComment;
						}
						else
							withinString = false;
					}
					else if (c == '\\')
					{
						Advance(i);
						if (i.mColumn < (int)line.size())
							line[i.mColumn].mMultiLineComment = inComment;
					}
				}
				else
				{
					if (c == '\"')
					{
						withinString = true;
						line[i.mColumn].mMultiLineComment = inComment;
					}
					else
					{
						auto pred = [](const char& a, const Glyph& b) { return a == b.mChar; };
						auto from = line.begin() + i.mColumn;
						auto& startStr = mLanguageDefinition.mCommentStart;
						if (i.mColumn + startStr.size() <= line.size() &&
							std::equal(startStr.begin(), startStr.end(), from, from + startStr.size(), pred))
							commentStart = i;

						inComment = commentStart <= i;

						line[i.mColumn].mMultiLineComment = inComment;

						auto& endStr = mLanguageDefinition.mCommentEnd;
						if (i.mColumn + 1 >= (int)endStr.size() &&
							std::equal(endStr.begin(), endStr.end(), from + 1 - endStr.size(), from + 1, pred))
							commentStart = end;
					}
				}
			}
		}
		mCheckMultilineComments = false;
		return;
	}

	if (mColorRangeMin < mColorRangeMax)
	{
		int to = std::min(mColorRangeMin + 10, mColorRangeMax);
		ColorizeRange(mColorRangeMin, to);
		mColorRangeMin = to;

		if (mColorRangeMax == mColorRangeMin)
		{
			mColorRangeMin = std::numeric_limits<int>::max();
			mColorRangeMax = 0;
		}
		return;
	}
}

int TextEditor::TextDistanceToLineStart(const Coordinates& aFrom) const
{
	auto& line = mLines[aFrom.mLine];
	auto len = 0;
	for (size_t it = 0u; it < line.size() && it < (unsigned)aFrom.mColumn; ++it)
		len = line[it].mChar == '\t' ? (len / mTabSize) * mTabSize + mTabSize : len + 1;
	return len;
}

void TextEditor::EnsureCursorVisible()
{
	if (!mWithinRender)
	{
		mScrollToCursor = true;
		return;
	}

	float scrollX = ImGui::GetScrollX();
	float scrollY = ImGui::GetScrollY();

	auto height = ImGui::GetWindowHeight();
	auto width = ImGui::GetWindowWidth();

	auto top = 1 + (int)ceil(scrollY / mCharAdvance.y);
	auto bottom = (int)ceil((scrollY + height) / mCharAdvance.y);

	auto left = (int)ceil(scrollX / mCharAdvance.x);
	auto right = (int)ceil((scrollX + width) / mCharAdvance.x);

	auto pos = GetActualCursorCoordinates();
	auto len = TextDistanceToLineStart(pos);

	if (pos.mLine < top)
		ImGui::SetScrollY(std::max(0.0f, (pos.mLine - 1) * mCharAdvance.y));
	if (pos.mLine > bottom - 4)
		ImGui::SetScrollY(std::max(0.0f, (pos.mLine + 4) * mCharAdvance.y - height));
	if (len + cTextStart < left + 4)
		ImGui::SetScrollX(std::max(0.0f, (len + cTextStart - 4) * mCharAdvance.x));
	if (len + cTextStart > right - 4)
		ImGui::SetScrollX(std::max(0.0f, (len + cTextStart + 4) * mCharAdvance.x - width));
}

int TextEditor::GetPageSize() const
{
	auto height = ImGui::GetWindowHeight() - 20.0f;
	return (int)floor(height / mCharAdvance.y);
}

TextEditor::UndoRecord::UndoRecord(
	const std::string& aAdded,
	const TextEditor::Coordinates aAddedStart,
	const TextEditor::Coordinates aAddedEnd,
	const std::string& aRemoved,
	const TextEditor::Coordinates aRemovedStart,
	const TextEditor::Coordinates aRemovedEnd,
	TextEditor::EditorState& aBefore,
	TextEditor::EditorState& aAfter)
	: mAdded(aAdded)
	, mAddedStart(aAddedStart)
	, mAddedEnd(aAddedEnd)
	, mRemoved(aRemoved)
	, mRemovedStart(aRemovedStart)
	, mRemovedEnd(aRemovedEnd)
	, mBefore(aBefore)
	, mAfter(aAfter)
{
	assert(mAddedStart <= mAddedEnd);
	assert(mRemovedStart <= mRemovedEnd);
}

void TextEditor::UndoRecord::Undo(TextEditor * aEditor)
{
	if (!mAdded.empty())
	{
		aEditor->DeleteRange(mAddedStart, mAddedEnd);
		aEditor->Colorize(mAddedStart.mLine - 1, mAddedEnd.mLine - mAddedStart.mLine + 2);
	}

	if (!mRemoved.empty())
	{
		auto start = mRemovedStart;
		aEditor->InsertTextAt(start, mRemoved.c_str());
		aEditor->Colorize(mRemovedStart.mLine - 1, mRemovedEnd.mLine - mRemovedStart.mLine + 2);
	}

	aEditor->mState = mBefore;
	aEditor->EnsureCursorVisible();

}

void TextEditor::UndoRecord::Redo(TextEditor * aEditor)
{
	if (!mRemoved.empty())
	{
		aEditor->DeleteRange(mRemovedStart, mRemovedEnd);
		aEditor->Colorize(mRemovedStart.mLine - 1, mRemovedEnd.mLine - mRemovedStart.mLine + 1);
	}

	if (!mAdded.empty())
	{
		auto start = mAddedStart;
		aEditor->InsertTextAt(start, mAdded.c_str());
		aEditor->Colorize(mAddedStart.mLine - 1, mAddedEnd.mLine - mAddedStart.mLine + 1);
	}

	aEditor->mState = mAfter;
	aEditor->EnsureCursorVisible();
}

TextEditor::LanguageDefinition TextEditor::LanguageDefinition::CPlusPlus()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const cppKeywords[] = {
			"alignas", "alignof", "and", "and_eq", "asm", "atomic_cancel", "atomic_commit", "atomic_noexcept", "auto", "bitand", "bitor", "bool", "break", "case", "catch", "char", "char16_t", "char32_t", "class",
			"compl", "concept", "const", "constexpr", "const_cast", "continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float",
			"for", "friend", "goto", "if", "import", "inline", "int", "long", "module", "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
			"register", "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "synchronized", "template", "this", "thread_local",
			"throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq"
		};
		for (auto& k : cppKeywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"abort", "abs", "acos", "asin", "atan", "atexit", "atof", "atoi", "atol", "ceil", "clock", "cosh", "ctime", "div", "exit", "fabs", "floor", "fmod", "getchar", "getenv", "isalnum", "isalpha", "isdigit", "isgraph",
			"ispunct", "isspace", "isupper", "kbhit", "log10", "log2", "log", "memcmp", "modf", "pow", "printf", "sprintf", "snprintf", "putchar", "putenv", "puts", "rand", "remove", "rename", "sinh", "sqrt", "srand", "strcat", "strcmp", "strerror", "time", "tolower", "toupper",
			"std", "string", "vector", "map", "unordered_map", "set", "unordered_set", "min", "max"
		};
		for (auto& k : identifiers)
			langDef.mIdentifiers.insert(k);

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("//.*", TokenType::Comment));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[ \t]*#[ \\t]*[a-zA-Z_]+", TokenType::Preprocessor));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("L?\\\"(\\\\.|[^\\\"])*\\\"", TokenType::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("\\'\\\\?[^\\']\\'", TokenType::CharLiteral));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("0[0-7]+[Uu]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[a-zA-Z_][a-zA-Z0-9_]*", TokenType::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", TokenType::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";

		langDef.mCaseSensitive = true;

		langDef.mName = "C++";

		inited = true;
	}
	return langDef;
}

TextEditor::LanguageDefinition TextEditor::LanguageDefinition::HLSL()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"AppendStructuredBuffer", "asm", "asm_fragment", "BlendState", "bool", "break", "Buffer", "ByteAddressBuffer", "case", "cbuffer", "centroid", "class", "column_major", "compile", "compile_fragment",
			"CompileShader", "const", "continue", "ComputeShader", "ConsumeStructuredBuffer", "default", "DepthStencilState", "DepthStencilView", "discard", "do", "double", "DomainShader", "dword", "else",
			"export", "extern", "false", "float", "for", "fxgroup", "GeometryShader", "groupshared", "half", "Hullshader", "if", "in", "inline", "inout", "InputPatch", "int", "interface", "line", "lineadj",
			"linear", "LineStream", "matrix", "min16float", "min10float", "min16int", "min12int", "min16uint", "namespace", "nointerpolation", "noperspective", "NULL", "out", "OutputPatch", "packoffset",
			"pass", "pixelfragment", "PixelShader", "point", "PointStream", "precise", "RasterizerState", "RenderTargetView", "return", "register", "row_major", "RWBuffer", "RWByteAddressBuffer", "RWStructuredBuffer",
			"RWTexture1D", "RWTexture1DArray", "RWTexture2D", "RWTexture2DArray", "RWTexture3D", "sample", "sampler", "SamplerState", "SamplerComparisonState", "shared", "snorm", "stateblock", "stateblock_state",
			"static", "string", "struct", "switch", "StructuredBuffer", "tbuffer", "technique", "technique10", "technique11", "texture", "Texture1D", "Texture1DArray", "Texture2D", "Texture2DArray", "Texture2DMS",
			"Texture2DMSArray", "Texture3D", "TextureCube", "TextureCubeArray", "true", "typedef", "triangle", "triangleadj", "TriangleStream", "uint", "uniform", "unorm", "unsigned", "vector", "vertexfragment",
			"VertexShader", "void", "volatile", "while",
			"bool1","bool2","bool3","bool4","double1","double2","double3","double4", "float1", "float2", "float3", "float4", "int1", "int2", "int3", "int4", "in", "out", "inout",
			"uint1", "uint2", "uint3", "uint4", "dword1", "dword2", "dword3", "dword4", "half1", "half2", "half3", "half4",
			"float1x1","float2x1","float3x1","float4x1","float1x2","float2x2","float3x2","float4x2",
			"float1x3","float2x3","float3x3","float4x3","float1x4","float2x4","float3x4","float4x4",
			"half1x1","half2x1","half3x1","half4x1","half1x2","half2x2","half3x2","half4x2",
			"half1x3","half2x3","half3x3","half4x3","half1x4","half2x4","half3x4","half4x4",
		};
		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"abort", "abs", "acos", "all", "AllMemoryBarrier", "AllMemoryBarrierWithGroupSync", "any", "asdouble", "asfloat", "asin", "asint", "asint", "asuint",
			"asuint", "atan", "atan2", "ceil", "CheckAccessFullyMapped", "clamp", "clip", "cos", "cosh", "countbits", "cross", "D3DCOLORtoUBYTE4", "ddx",
			"ddx_coarse", "ddx_fine", "ddy", "ddy_coarse", "ddy_fine", "degrees", "determinant", "DeviceMemoryBarrier", "DeviceMemoryBarrierWithGroupSync",
			"distance", "dot", "dst", "errorf", "EvaluateAttributeAtCentroid", "EvaluateAttributeAtSample", "EvaluateAttributeSnapped", "exp", "exp2",
			"f16tof32", "f32tof16", "faceforward", "firstbithigh", "firstbitlow", "floor", "fma", "fmod", "frac", "frexp", "fwidth", "GetRenderTargetSampleCount",
			"GetRenderTargetSamplePosition", "GroupMemoryBarrier", "GroupMemoryBarrierWithGroupSync", "InterlockedAdd", "InterlockedAnd", "InterlockedCompareExchange",
			"InterlockedCompareStore", "InterlockedExchange", "InterlockedMax", "InterlockedMin", "InterlockedOr", "InterlockedXor", "isfinite", "isinf", "isnan",
			"ldexp", "length", "lerp", "lit", "log", "log10", "log2", "mad", "max", "min", "modf", "msad4", "mul", "noise", "normalize", "pow", "printf",
			"Process2DQuadTessFactorsAvg", "Process2DQuadTessFactorsMax", "Process2DQuadTessFactorsMin", "ProcessIsolineTessFactors", "ProcessQuadTessFactorsAvg",
			"ProcessQuadTessFactorsMax", "ProcessQuadTessFactorsMin", "ProcessTriTessFactorsAvg", "ProcessTriTessFactorsMax", "ProcessTriTessFactorsMin",
			"radians", "rcp", "reflect", "refract", "reversebits", "round", "rsqrt", "saturate", "sign", "sin", "sincos", "sinh", "smoothstep", "sqrt", "step",
			"tan", "tanh", "tex1D", "tex1D", "tex1Dbias", "tex1Dgrad", "tex1Dlod", "tex1Dproj", "tex2D", "tex2D", "tex2Dbias", "tex2Dgrad", "tex2Dlod", "tex2Dproj",
			"tex3D", "tex3D", "tex3Dbias", "tex3Dgrad", "tex3Dlod", "tex3Dproj", "texCUBE", "texCUBE", "texCUBEbias", "texCUBEgrad", "texCUBElod", "texCUBEproj", "transpose", "trunc"
		};
		for (auto& k : identifiers)
			langDef.mIdentifiers.insert(k);

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("//.*", TokenType::Comment));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[ \t]*#[ \\t]*[a-zA-Z_]+", TokenType::Preprocessor));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("L?\\\"(\\\\.|[^\\\"])*\\\"", TokenType::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("\\'\\\\?[^\\']\\'", TokenType::CharLiteral));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("0[0-7]+[Uu]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[a-zA-Z_][a-zA-Z0-9_]*", TokenType::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", TokenType::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";

		langDef.mCaseSensitive = true;

		langDef.mName = "HLSL";

		inited = true;
	}
	return langDef;
}

TextEditor::LanguageDefinition TextEditor::LanguageDefinition::GLSL()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short",
			"signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary",
			"_Noreturn", "_Static_assert", "_Thread_local"
		};
		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"abort", "abs", "acos", "asin", "atan", "atexit", "atof", "atoi", "atol", "ceil", "clock", "cosh", "ctime", "div", "exit", "fabs", "floor", "fmod", "getchar", "getenv", "isalnum", "isalpha", "isdigit", "isgraph",
			"ispunct", "isspace", "isupper", "kbhit", "log10", "log2", "log", "memcmp", "modf", "pow", "putchar", "putenv", "puts", "rand", "remove", "rename", "sinh", "sqrt", "srand", "strcat", "strcmp", "strerror", "time", "tolower", "toupper"
		};
		for (auto& k : identifiers)
			langDef.mIdentifiers.insert(k);

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("//.*", TokenType::Comment));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[ \t]*#[ \\t]*[a-zA-Z_]+", TokenType::Preprocessor));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("L?\\\"(\\\\.|[^\\\"])*\\\"", TokenType::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("\\'\\\\?[^\\']\\'", TokenType::CharLiteral));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("0[0-7]+[Uu]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[a-zA-Z_][a-zA-Z0-9_]*", TokenType::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", TokenType::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";

		langDef.mCaseSensitive = true;

		langDef.mName = "GLSL";

		inited = true;
	}
	return langDef;
}

TextEditor::LanguageDefinition TextEditor::LanguageDefinition::C()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short",
			"signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary",
			"_Noreturn", "_Static_assert", "_Thread_local"
		};
		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"abort", "abs", "acos", "asin", "atan", "atexit", "atof", "atoi", "atol", "ceil", "clock", "cosh", "ctime", "div", "exit", "fabs", "floor", "fmod", "getchar", "getenv", "isalnum", "isalpha", "isdigit", "isgraph",
			"ispunct", "isspace", "isupper", "kbhit", "log10", "log2", "log", "memcmp", "modf", "pow", "putchar", "putenv", "puts", "rand", "remove", "rename", "sinh", "sqrt", "srand", "strcat", "strcmp", "strerror", "time", "tolower", "toupper"
		};
		for (auto& k : identifiers)
			langDef.mIdentifiers.insert(k);

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("//.*", TokenType::Comment));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[ \t]*#[ \\t]*[a-zA-Z_]+", TokenType::Preprocessor));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("L?\\\"(\\\\.|[^\\\"])*\\\"", TokenType::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("\\'\\\\?[^\\']\\'", TokenType::CharLiteral));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("0[0-7]+[Uu]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[a-zA-Z_][a-zA-Z0-9_]*", TokenType::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", TokenType::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";

		langDef.mCaseSensitive = true;

		langDef.mName = "C";

		inited = true;
	}
	return langDef;
}

TextEditor::LanguageDefinition TextEditor::LanguageDefinition::SQL()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"ADD", "EXCEPT", "PERCENT", "ALL", "EXEC", "PLAN", "ALTER", "EXECUTE", "PRECISION", "AND", "EXISTS", "PRIMARY", "ANY", "EXIT", "PRINT", "AS", "FETCH", "PROC", "ASC", "FILE", "PROCEDURE",
			"AUTHORIZATION", "FILLFACTOR", "PUBLIC", "BACKUP", "FOR", "RAISERROR", "BEGIN", "FOREIGN", "READ", "BETWEEN", "FREETEXT", "READTEXT", "BREAK", "FREETEXTTABLE", "RECONFIGURE",
			"BROWSE", "FROM", "REFERENCES", "BULK", "FULL", "REPLICATION", "BY", "FUNCTION", "RESTORE", "CASCADE", "GOTO", "RESTRICT", "CASE", "GRANT", "RETURN", "CHECK", "GROUP", "REVOKE",
			"CHECKPOINT", "HAVING", "RIGHT", "CLOSE", "HOLDLOCK", "ROLLBACK", "CLUSTERED", "IDENTITY", "ROWCOUNT", "COALESCE", "IDENTITY_INSERT", "ROWGUIDCOL", "COLLATE", "IDENTITYCOL", "RULE",
			"COLUMN", "IF", "SAVE", "COMMIT", "IN", "SCHEMA", "COMPUTE", "INDEX", "SELECT", "CONSTRAINT", "INNER", "SESSION_USER", "CONTAINS", "INSERT", "SET", "CONTAINSTABLE", "INTERSECT", "SETUSER",
			"CONTINUE", "INTO", "SHUTDOWN", "CONVERT", "IS", "SOME", "CREATE", "JOIN", "STATISTICS", "CROSS", "KEY", "SYSTEM_USER", "CURRENT", "KILL", "TABLE", "CURRENT_DATE", "LEFT", "TEXTSIZE",
			"CURRENT_TIME", "LIKE", "THEN", "CURRENT_TIMESTAMP", "LINENO", "TO", "CURRENT_USER", "LOAD", "TOP", "CURSOR", "NATIONAL", "TRAN", "DATABASE", "NOCHECK", "TRANSACTION",
			"DBCC", "NONCLUSTERED", "TRIGGER", "DEALLOCATE", "NOT", "TRUNCATE", "DECLARE", "NULL", "TSEQUAL", "DEFAULT", "NULLIF", "UNION", "DELETE", "OF", "UNIQUE", "DENY", "OFF", "UPDATE",
			"DESC", "OFFSETS", "UPDATETEXT", "DISK", "ON", "USE", "DISTINCT", "OPEN", "USER", "DISTRIBUTED", "OPENDATASOURCE", "VALUES", "DOUBLE", "OPENQUERY", "VARYING","DROP", "OPENROWSET", "VIEW",
			"DUMMY", "OPENXML", "WAITFOR", "DUMP", "OPTION", "WHEN", "ELSE", "OR", "WHERE", "END", "ORDER", "WHILE", "ERRLVL", "OUTER", "WITH", "ESCAPE", "OVER", "WRITETEXT"
		};

		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"ABS",  "ACOS",  "ADD_MONTHS",  "ASCII",  "ASCIISTR",  "ASIN",  "ATAN",  "ATAN2",  "AVG",  "BFILENAME",  "BIN_TO_NUM",  "BITAND",  "CARDINALITY",  "CASE",  "CAST",  "CEIL",			"CHARTOROWID",  "CHR",  "COALESCE",  "COMPOSE",  "CONCAT",  "CONVERT",  "CORR",  "COS",  "COSH",  "COUNT",  "COVAR_POP",  "COVAR_SAMP",  "CUME_DIST",  "CURRENT_DATE",			"CURRENT_TIMESTAMP",  "DBTIMEZONE",  "DECODE",  "DECOMPOSE",  "DENSE_RANK",  "DUMP",  "EMPTY_BLOB",  "EMPTY_CLOB",  "EXP",  "EXTRACT",  "FIRST_VALUE",  "FLOOR",  "FROM_TZ",  "GREATEST",			"GROUP_ID",  "HEXTORAW",  "INITCAP",  "INSTR",  "INSTR2",  "INSTR4",  "INSTRB",  "INSTRC",  "LAG",  "LAST_DAY",  "LAST_VALUE",  "LEAD",  "LEAST",  "LENGTH",  "LENGTH2",  "LENGTH4",			"LENGTHB",  "LENGTHC",  "LISTAGG",  "LN",  "LNNVL",  "LOCALTIMESTAMP",  "LOG",  "LOWER",  "LPAD",  "LTRIM",  "MAX",  "MEDIAN",  "MIN",  "MOD",  "MONTHS_BETWEEN",  "NANVL",  "NCHR",			"NEW_TIME",  "NEXT_DAY",  "NTH_VALUE",  "NULLIF",  "NUMTODSINTERVAL",  "NUMTOYMINTERVAL",  "NVL",  "NVL2",  "POWER",  "RANK",  "RAWTOHEX",  "REGEXP_COUNT",  "REGEXP_INSTR",			"REGEXP_REPLACE",  "REGEXP_SUBSTR",  "REMAINDER",  "REPLACE",  "ROUND",  "ROWNUM",  "RPAD",  "RTRIM",  "SESSIONTIMEZONE",  "SIGN",  "SIN",  "SINH",			"SOUNDEX",  "SQRT",  "STDDEV",  "SUBSTR",  "SUM",  "SYS_CONTEXT",  "SYSDATE",  "SYSTIMESTAMP",  "TAN",  "TANH",  "TO_CHAR",  "TO_CLOB",  "TO_DATE",  "TO_DSINTERVAL",  "TO_LOB",			"TO_MULTI_BYTE",  "TO_NCLOB",  "TO_NUMBER",  "TO_SINGLE_BYTE",  "TO_TIMESTAMP",  "TO_TIMESTAMP_TZ",  "TO_YMINTERVAL",  "TRANSLATE",  "TRIM",  "TRUNC", "TZ_OFFSET",  "UID",  "UPPER",			"USER",  "USERENV",  "VAR_POP",  "VAR_SAMP",  "VARIANCE",  "VSIZE "
		};
		for (auto& k : identifiers)
			langDef.mIdentifiers.insert(k);

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("\\-\\-.*", TokenType::Comment));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("L?\\\"(\\\\.|[^\\\"])*\\\"", TokenType::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("\\\'[^\\\']*\\\'", TokenType::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("0[0-7]+[Uu]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[a-zA-Z_][a-zA-Z0-9_]*", TokenType::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", TokenType::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";

		langDef.mCaseSensitive = false;

		langDef.mName = "SQL";

		inited = true;
	}
	return langDef;
}

TextEditor::LanguageDefinition TextEditor::LanguageDefinition::AngelScript()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"and", "abstract", "auto", "bool", "break", "case", "cast", "class", "const", "continue", "default", "do", "double", "else", "enum", "false", "final", "float", "for",
			"from", "funcdef", "function", "get", "if", "import", "in", "inout", "int", "interface", "int8", "int16", "int32", "int64", "is", "mixin", "namespace", "not",
			"null", "or", "out", "override", "private", "protected", "return", "set", "shared", "super", "switch", "this ", "true", "typedef", "uint", "uint8", "uint16", "uint32",
			"uint64", "void", "while", "xor"
		};

		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"cos", "sin", "tab", "acos", "asin", "atan", "atan2", "cosh", "sinh", "tanh", "log", "log10", "pow", "sqrt", "abs", "ceil", "floor", "fraction", "closeTo", "fpFromIEEE", "fpToIEEE",
			"complex", "opEquals", "opAddAssign", "opSubAssign", "opMulAssign", "opDivAssign", "opAdd", "opSub", "opMul", "opDiv"
		};
		for (auto& k : identifiers)
			langDef.mIdentifiers.insert(k);

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("//.*", TokenType::Comment));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("L?\\\"(\\\\.|[^\\\"])*\\\"", TokenType::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("\\'\\\\?[^\\']\\'", TokenType::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("0[0-7]+[Uu]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[a-zA-Z_][a-zA-Z0-9_]*", TokenType::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", TokenType::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";

		langDef.mCaseSensitive = true;

		langDef.mName = "AngelScript";

		inited = true;
	}
	return langDef;
}

TextEditor::LanguageDefinition TextEditor::LanguageDefinition::Lua()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"and", "break", "do", "", "else", "elseif", "end", "false", "for", "function", "if", "in", "", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"
		};

		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"assert", "collectgarbage", "dofile", "error", "getmetatable", "ipairs", "loadfile", "load", "loadstring",  "next",  "pairs",  "pcall",  "print",  "rawequal",  "rawlen",  "rawget",  "rawset",
			"select",  "setmetatable",  "tonumber",  "tostring",  "type",  "xpcall",  "_G",  "_VERSION","arshift", "band", "bnot", "bor", "bxor", "btest", "extract", "lrotate", "lshift", "replace", 
			"rrotate", "rshift", "create", "resume", "running", "status", "wrap", "yield", "isyieldable", "debug","getuservalue", "gethook", "getinfo", "getlocal", "getregistry", "getmetatable", 
			"getupvalue", "upvaluejoin", "upvalueid", "setuservalue", "sethook", "setlocal", "setmetatable", "setupvalue", "traceback", "close", "flush", "input", "lines", "open", "output", "popen", 
			"read", "tmpfile", "type", "write", "close", "flush", "lines", "read", "seek", "setvbuf", "write", "__gc", "__tostring", "abs", "acos", "asin", "atan", "ceil", "cos", "deg", "exp", "tointeger",
			"floor", "fmod", "ult", "log", "max", "min", "modf", "rad", "random", "randomseed", "sin", "sqrt", "string", "tan", "type", "atan2", "cosh", "sinh", "tanh",
			 "pow", "frexp", "ldexp", "log10", "pi", "huge", "maxinteger", "mininteger", "loadlib", "searchpath", "seeall", "preload", "cpath", "path", "searchers", "loaded", "module", "require", "clock",
			 "date", "difftime", "execute", "exit", "getenv", "remove", "rename", "setlocale", "time", "tmpname", "byte", "char", "dump", "find", "format", "gmatch", "gsub", "len", "lower", "match", "rep",
			 "reverse", "sub", "upper", "pack", "packsize", "unpack", "concat", "maxn", "insert", "pack", "unpack", "remove", "move", "sort", "offset", "codepoint", "char", "len", "codes", "charpattern",
			 "coroutine", "table", "io", "os", "string", "utf8", "bit32", "math", "debug", "package"
		};
		for (auto& k : identifiers)
			langDef.mIdentifiers.insert(k);

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("\\-\\-.*", TokenType::Comment));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("L?\\\"(\\\\.|[^\\\"])*\\\"", TokenType::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("\\\'[^\\\']*\\\'", TokenType::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", TokenType::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[a-zA-Z_][a-zA-Z0-9_]*", TokenType::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, TokenType>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", TokenType::Punctuation));

		langDef.mCommentStart = "\\-\\-\\[\\[";
		langDef.mCommentEnd = "\\]\\]";

		langDef.mCaseSensitive = true;

		langDef.mName = "Lua";

		inited = true;
	}
	return langDef;
}
