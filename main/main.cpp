#include <iostream>
#include <vector>
#include <conio.h>
#include <windows.h>
#include <string>
#include <cstring>
#include <stack>
#include <fstream>
#include <filesystem>
#include <algorithm>

using namespace std;
namespace fs = std::filesystem;

// Dosya ve klasör bilgilerini tutan yapı
struct FileItem
{
    string name;
    string fullPath;
    bool isDirectory;
};

// Her bir karakteri temsil eden düğüm yapısı
struct Node
{
    char data;          // Karakterin kendisi
    int color;          // Karakterin rengi (mavi: arama sonucu, sarı: seçim)
    Node* next;         // Bir sonrakinin adres
    Node* prev;         // Bir öncekinin adres

    // Yeni bir düğüm oluşturulduğunda çalışan hazırlayıcı (Constructor)

    Node(char c) {
        data = c;
        color = 7;      // Varsayılan olarak beyaz renk
        next = nullptr; // Henüz birine bağlanmadığı için boş
        prev = nullptr;
    }
};

// Her bir satırı yöneten yapı
struct Line
{
    Node* head;         // Satırın başındaki karakter
    Node* tail;         // Satırın sonundaki karakter
    int lineLength;     // Satırdaki karakter sayısı
    Line()
    {
        head = nullptr;
        tail = nullptr;
        lineLength = 0;
    }
};

// Arama sonuçlarını saklamak ve aktif olanı vurgulamak için yapı
struct EditorState
{
    vector<string> lines;
    int cursorRow;
    int cursorCol;
};

//Olduğumuz satır ve karakteri takip eden yapı
struct Editor
{
    vector<Line*> lines;    // Tüm satırları tutan liste
    int currentRow;         // İmlecin bulunduğu satır indeksi (0, 1, 2...)
    int currentCol;         // İmlecin bulunduğu sütun indeksi (0, 1, 2...)

    // İmlecin bağlı olduğu fiziksel düğümü takip etmek için:
    Node* currentNode;      // Üzerinde bulunduğumuz karakter düğümü

    Editor()
    {
        // Uygulama başladığında doğrudan yeni bir dosya (boş bir satır) açılır 
        Line* firstLine = new Line();
        lines.push_back(firstLine);
        currentRow = 0;
        currentCol = 0;
        currentNode = nullptr;
    }

    int anchorRow = -1; // Seçim başlangıç satırı
    int anchorCol = -1; // Seçim başlangıç sütunu
    bool isSelecting = false;
    vector<EditorState> undoStack;
};

// Arama sonuçlarını saklamak ve aktif olanı vurgulamak için yapı
struct Match
{
    int row;
    int col;
};

// Imleç konumu değişimi için
void gotoxy(int x, int y)
{
    COORD coord;
    coord.X = x;
    coord.Y = y;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}

// Toolbox çizimi
void drawToolbox()
{
    // Ekranın en üstünü temiz tutalım
    gotoxy(0, 0);
    cout << "----------------------------------------------------------------------------------------------------------" << endl;
    cout << " Ac (CTRL+O) | Kaydet (CTRL+S) | Bul (CTRL+F) | Degistir (CTRL+H) | Kes (CTRL+X) | Kopya (CTRL+C) | Yapistir (CTRL+V) | Geri (CTRL+Z) " << endl;
    cout << "----------------------------------------------------------------------------------------------------------" << endl;
}

// İmleci gösterme/gizleme fonksiyonu
void showCursor(bool showFlag)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(out, &cursorInfo);
    cursorInfo.bVisible = showFlag; // true: göster, false: gizle
    SetConsoleCursorInfo(out, &cursorInfo);
}

// Karakter silme fonksiyonu
void deleteCharacter(Editor& ed)
{
    Line* curLine = ed.lines[ed.currentRow];
    // Satır boşsa veya imleç en baştaysa silinecek bir şey yok
    if (curLine->head == nullptr || ed.currentCol == 0) return;
    Node* nodeToDelete = nullptr;
    // 1. DURUM: En sondaki karakteri silme
    if (ed.currentCol == curLine->lineLength)
    {
        nodeToDelete = curLine->tail;
        if (curLine->head == curLine->tail)// Tek karakter varsa
        {
            curLine->head = curLine->tail = nullptr;
        }
        else
        {
            curLine->tail = nodeToDelete->prev;
            curLine->tail->next = nullptr;
        }
    }
    // 2. DURUM: Aradan bir karakter silme (İmlecin solundakini siler)
    else
    {
        // Silinecek düğümü bul (İmlecin hemen solundaki düğüm)
        nodeToDelete = curLine->head;
        for (int i = 0; i < ed.currentCol - 1; i++)
        {
            nodeToDelete = nodeToDelete->next;
        }
        // Eğer en baştaki düğüm siliniyorsa
        if (nodeToDelete == curLine->head)
        {
            curLine->head = nodeToDelete->next;
            if (curLine->head) curLine->head->prev = nullptr;
        }
        else
        {
            // Bağları birbirine bağla (Silinecek olanı aradan çıkar)
            nodeToDelete->prev->next = nodeToDelete->next;
            if (nodeToDelete->next)
            {
                nodeToDelete->next->prev = nodeToDelete->prev;
            }
        }
    }
    delete nodeToDelete; // Belleği temizle
    curLine->lineLength--;
    ed.currentCol--; // İmleci sola kaydır
    // Aradan silme yapıldığında satırın geri kalanı kayacağı için ekranı tazele
    // renderEditor zaten her döngüde çalıştığı için görsel güncellenecektir.
}

// Backspace tuşuna basıldığında yapılacak işlemler
void handleBackspace(Editor& ed)
{
    Line* curLine = ed.lines[ed.currentRow];

    // 1. DURUM: Satır içinde silecek karakter var
    if (ed.currentCol > 0)
    {
        deleteCharacter(ed);
    }
    // 2. DURUM: Satırın en başındayız ve bu satırı tamamen silip yukarı çıkmak istiyoruz
    else if (ed.currentRow > 0)
    {
        // Üst satırı ve onun son sütun pozisyonunu belirle
        Line* prevLine = ed.lines[ed.currentRow - 1];
        int newCol = prevLine->lineLength;
        // Eğer mevcut satırda karakterler varsa (araya girip silme yapıyorsak)
        // Bu karakterleri üst satırın sonuna bağlamamız gerekir.
        if (curLine->head != nullptr)
        {
            if (prevLine->head == nullptr)
            {
                prevLine->head = curLine->head;
                prevLine->tail = curLine->tail;
            }
            else
            {
                prevLine->tail->next = curLine->head;
                curLine->head->prev = prevLine->tail;
                prevLine->tail = curLine->tail;
            }
            prevLine->lineLength += curLine->lineLength;
        }
        // Mevcut (boşalan) satırı bellekten ve listeden sil 
        delete ed.lines[ed.currentRow];
        ed.lines.erase(ed.lines.begin() + ed.currentRow);
        // İmleci yukarı taşı
        ed.currentRow--;
        ed.currentCol = newCol;
        // Ekran düzeni (satır numaraları) değiştiği için temizlik şart 
        system("cls");
        drawToolbox();
    }
}

// Seçili alanı silme fonksiyonu
void deleteSelectedArea(Editor& ed) {
    if (!ed.isSelecting || ed.anchorRow == -1) return;

    int startR, endR, startC, endC;

    if (ed.currentRow < ed.anchorRow ||
        (ed.currentRow == ed.anchorRow && ed.currentCol < ed.anchorCol)) {
        startR = ed.currentRow;
        startC = ed.currentCol;
        endR = ed.anchorRow;
        endC = ed.anchorCol;
    }
    else {
        startR = ed.anchorRow;
        startC = ed.anchorCol;
        endR = ed.currentRow;
        endC = ed.currentCol;
    }

    // Tek satırlı seçim
    if (startR == endR) {
        ed.currentRow = startR;
        ed.currentCol = endC;

        while (ed.currentCol > startC) {
            deleteCharacter(ed);
        }
    }
    // Çok satırlı seçim
    else {
        ed.currentRow = endR;
        ed.currentCol = endC;

        while (!(ed.currentRow == startR && ed.currentCol == startC)) {
            if (ed.currentCol > 0) {
                deleteCharacter(ed);
            }
            else {
                handleBackspace(ed);
            }
        }
    }

    ed.isSelecting = false;
    ed.anchorRow = -1;
    ed.anchorCol = -1;
}

//Karakter ekleme fonksiyonu
void insertCharacter(Editor& ed, char c)
{
    if (ed.isSelecting) deleteSelectedArea(ed); // Seçili alan varsa önce sil
    Node* newNode = new Node(c);
    Line* curLine = ed.lines[ed.currentRow];

    // 1. DURUM: Satır tamamen boşsa (İlk karakter)
    if (curLine->head == nullptr)
    {
        curLine->head = curLine->tail = newNode;
    }
    // 2. DURUM: En başa ekleme (currentCol == 0)
    else if (ed.currentCol == 0)
    {
        newNode->next = curLine->head;
        curLine->head->prev = newNode;
        curLine->head = newNode;
    }
    // 3. DURUM: En sona ekleme (currentCol == satır uzunluğu)
    else if (ed.currentCol == curLine->lineLength)
    {
        newNode->prev = curLine->tail;
        curLine->tail->next = newNode;
        curLine->tail = newNode;
    }

    // 4. DURUM: Araya ekleme (En zor kısım)
    else
    {
        // İmlecin olduğu yerdeki düğümü bulmamız lazım
        Node* temp = curLine->head;
        for (int i = 0; i < ed.currentCol - 1; i++)
        {
            temp = temp->next;
        }
        // temp şu an eklenecek yerden bir önceki düğüm
        newNode->next = temp->next;
        newNode->prev = temp;
        if (temp->next != nullptr)
        {
            temp->next->prev = newNode;
        }
        temp->next = newNode;
    }
    curLine->lineLength++;
    ed.currentCol++;
}

// Kelime silme fonksiyonu 
void deleteWord(Editor& ed)
{
    Line* curLine = ed.lines[ed.currentRow];
    // Satır başındaysak üst satıra çık (Normal Backspace gibi)
    if (ed.currentCol == 0)
    {
        handleBackspace(ed);
        return;
    }
    // Kelime silme mantığı:
    // 1. Önce imlecin hemen solundaki boşlukları temizle
    while (ed.currentCol > 0)
    {
        // currentCol - 1 konumundaki düğümü bulmak için prev kullanabiliriz 
        // ama senin yapında currentCol bir sayı. O yüzden en güvenlisi:
        Node* target = curLine->head;
        for (int i = 0; i < ed.currentCol - 1; i++) target = target->next;

        if (target->data == ' ')
        {
            deleteCharacter(ed);
        }
        else break;
    }
    // 2. Şimdi bir kelime bulana kadar (boşluğa rastlayana kadar) sil
    while (ed.currentCol > 0)
    {
        Node* target = curLine->head;
        for (int i = 0; i < ed.currentCol - 1; i++) target = target->next;

        if (target->data != ' ')
        {
            deleteCharacter(ed);
        }
        else break;
    }
}

// Metni değiştirme fonksiyonu
void replaceText(Editor& ed, int row, int col, int oldLen, string newText)
{
    Line* line = ed.lines[row];
    Node* current = line->head;

    // 1. Kelimenin başladığı yere git
    for (int i = 0; i < col && current; i++) current = current->next;
    if (!current) return;

    Node* before = current->prev;
    Node* after = current;

    // 2. Eski düğümleri sil

    for (int i = 0; i < oldLen && after; i++)
    {
        Node* temp = after;
        after = after->next;

        // Baş ve son koruması
        if (temp == line->head) line->head = after;
        if (temp == line->tail) line->tail = before;

        delete temp;
    }

    // 3. Kopan iki ucu (before ve after) birbirine bağla
    if (before) before->next = after;
    if (after) after->prev = before;

    // 4. Yeni kelimeyi 'before'dan başlayarak araya sok
    Node* last = before;
    for (char c : newText)
    {
        Node* newNode = new Node(c);
        if (last == nullptr)// Satırın en başındaysak
        {
            newNode->next = line->head;
            if (line->head) line->head->prev = newNode;
            line->head = newNode;
            if (!line->tail) line->tail = newNode;
        }
        else // Araya veya sona ekleme
        {
            newNode->next = last->next;
            newNode->prev = last;
            if (last->next) last->next->prev = newNode;
            last->next = newNode;
            if (last == line->tail) line->tail = newNode;
        }
        last = newNode;
    }
    line->lineLength = line->lineLength - oldLen + (int)newText.length();
}

// Metni ekrana yazdıran fonksiyon
void renderEditor(Editor& ed, string statusMessage = "")
{
    showCursor(false);
    gotoxy(0, 3);
    cout << "                                                                                                    ";
    gotoxy(0, 3);
    cout << statusMessage;

    int startRow = 5;

    // Seçim sınırlarını hesapla
    int selStartR = -1, selStartC = -1, selEndR = -1, selEndC = -1;
    if (ed.isSelecting)
    {
        if (ed.currentRow < ed.anchorRow || (ed.currentRow == ed.anchorRow && ed.currentCol < ed.anchorCol))
        {
            selStartR = ed.currentRow; selStartC = ed.currentCol;
            selEndR = ed.anchorRow; selEndC = ed.anchorCol;
        }
        else
        {
            selStartR = ed.anchorRow; selStartC = ed.anchorCol;
            selEndR = ed.currentRow; selEndC = ed.currentCol;
        }
    }

    for (int i = 0; i < ed.lines.size(); i++)
    {
        gotoxy(0, startRow + i);
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
        cout << i + 1 << " | ";

        Node* temp = ed.lines[i]->head;
        int currC = 0;
        while (temp != nullptr)
        {
            bool boya = false;
            if (ed.isSelecting)
            {
                // Matematiksel kontrol: Bu karakter (i, writtenChars) seçili alanda mı?
                // Basitçe: Anchor ile Current imleç arasındaysa boya.
                int r = i;
                int c = currC;

                // Seçim yönünü bul (İleri mi geri mi?)   
                int startR = min(ed.anchorRow, ed.currentRow);
                int endR = max(ed.anchorRow, ed.currentRow);
                int startC = (ed.anchorRow < ed.currentRow) ? ed.anchorCol : (ed.anchorRow > ed.currentRow ? ed.currentCol : min(ed.anchorCol, ed.currentCol));
                int endC = (ed.anchorRow < ed.currentRow) ? ed.currentCol : (ed.anchorRow > ed.currentRow ? ed.anchorCol : max(ed.anchorCol, ed.currentCol));

                // Bu karakter aralıkta mı? (Bu kısım biraz karmaşık gelebilir ama standart seçim mantığıdır)
                if (r > startR && r < endR) boya = true;
                else if (startR == endR && r == startR) boya = (c >= min(ed.anchorCol, ed.currentCol) && c < max(ed.anchorCol, ed.currentCol));
                else if (r == startR) boya = (c >= (startR == ed.anchorRow ? ed.anchorCol : ed.currentCol));
                else if (r == endR) boya = (c < (endR == ed.anchorRow ? ed.anchorCol : ed.currentCol));
            }

            if (boya) { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 224); } // SARI ARKA PLAN
            else { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), temp->color); }

            cout << temp->data;
            temp = temp->next;
            currC++;
        }

        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);
        for (int k = 0; k < (100 - currC); k++) cout << " ";
    }
    showCursor(true);
}

// Seçili metni alma fonksiyonu
string getSelectedText(Editor& ed) {
    if (!ed.isSelecting || ed.anchorRow == -1) return "";
    int startR, endR, startC, endC;

    if (ed.currentRow < ed.anchorRow || (ed.currentRow == ed.anchorRow && ed.currentCol < ed.anchorCol))
    {
        startR = ed.currentRow;
        startC = ed.currentCol;
        endR = ed.anchorRow;
        endC = ed.anchorCol;
    }
    else
    {
        startR = ed.anchorRow;
        startC = ed.anchorCol;
        endR = ed.currentRow;
        endC = ed.currentCol;
    }

    string selectedText = "";

    for (int r = startR; r <= endR; r++)
    {
        Line* line = ed.lines[r];
        Node* temp = line->head;
        int c = 0;

        while (temp != nullptr)
        {
            bool include = false;
            if (startR == endR)
            {
                include = (c >= startC && c < endC);
            }
            else if (r == startR)
            {
                include = (c >= startC);
            }
            else if (r == endR)
            {
                include = (c < endC);
            }
            else
            {
                include = true;
            }
            if (include) selectedText += temp->data;
            temp = temp->next;
            c++;
        }
        if (r != endR) selectedText += '\n';
    }
    return selectedText;
}

// Arama yapıp sonuçları renklendiren fonksiyon
vector<Match> searchAndHighlight(Editor& ed, string query, int activeIndex, int activeColor = 31, int passiveColor = 8)
{
    vector<Match> matches;
    // Tüm renkleri sıfırla

    for (Line* line : ed.lines)
    {
        Node* t = line->head;
        while (t) { t->color = 7; t = t->next; }
    }

    if (query.empty()) return matches;
    for (int i = 0; i < ed.lines.size(); i++)
    {
        string lineText = "";
        Node* temp = ed.lines[i]->head;
        while (temp)
        {
            lineText += temp->data;
            temp = temp->next;
        }

        size_t pos = lineText.find(query);
        while (pos != string::npos)
        {
            matches.push_back({ i, (int)pos });
            Node* highlightNode = ed.lines[i]->head;
            for (int k = 0; k < pos; k++) highlightNode = highlightNode->next;

            for (int k = 0; k < query.length(); k++)
            {
                if (highlightNode)
                {
                    // Eğer aktif seçilen eşleşmeyse aktif rengi, değilse pasif rengi bas
                    if ((int)matches.size() - 1 == activeIndex)
                    {
                        highlightNode->color = activeColor;
                    }
                    else
                    {
                        highlightNode->color = passiveColor;
                    }
                    highlightNode = highlightNode->next;
                }
            }
            pos = lineText.find(query, pos + 1);
        }
    }
    return matches;
}

// Yeni satır ekleme fonksiyonu
void insertNewLine(Editor& ed) {
    Line* curLine = ed.lines[ed.currentRow];
    Line* newLine = new Line();

    // İmleç satırın ortasındaysa sağ tarafı yeni satıra taşı
    if (ed.currentCol < curLine->lineLength) {
        Node* temp = curLine->head;

        for (int i = 0; i < ed.currentCol; i++)
        {
            temp = temp->next;
        }
        newLine->head = temp;
        newLine->tail = curLine->tail;
        newLine->lineLength = curLine->lineLength - ed.currentCol;

        if (temp->prev)
        {
            temp->prev->next = nullptr;
            curLine->tail = temp->prev;
        }
        else
        {
            curLine->head = curLine->tail = nullptr;
        }

        temp->prev = nullptr;
        curLine->lineLength = ed.currentCol;
    }
    ed.lines.insert(ed.lines.begin() + ed.currentRow + 1, newLine);
    ed.currentRow++;
    ed.currentCol = 0;
}

// Panodan yapıştırma fonksiyonu
void pasteText(Editor& ed, const string& text) {
    for (char c : text)
    {
        if (c == '\n')
        {
            insertNewLine(ed);
        }
        else
        {
            insertCharacter(ed, c);
        }
    }
}

// Panoya kopyalama fonksiyonu (Windows için)
void copyToWindowsClipboard(const string& text) {
    if (!OpenClipboard(nullptr)) return;

    EmptyClipboard();

    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (hGlob)
    {
        char* buffer = (char*)GlobalLock(hGlob);
        if (buffer)
        {
            strcpy_s(buffer, text.size() + 1, text.c_str());
            GlobalUnlock(hGlob);
            SetClipboardData(CF_TEXT, hGlob);
        }
        else
        {
            GlobalFree(hGlob);
        }
    }
    CloseClipboard();
}

// Panodan metin alma fonksiyonu (Windows için)
string getFromWindowsClipboard() {
    string result = "";

    if (!OpenClipboard(nullptr)) return result;
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData != nullptr)
    {
        char* pszText = static_cast<char*>(GlobalLock(hData));
        if (pszText != nullptr)
        {
            result = pszText;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return result;
}

// Editörün içeriğini string listesi olarak alma fonksiyonu (Dosyaya kaydetmek için)
vector<string> getEditorContent(Editor& ed)
{
    vector<string> result;
    for (Line* line : ed.lines)
    {
        string s = "";
        Node* temp = line->head;

        while (temp)
        {
            s += temp->data;
            temp = temp->next;
        }
        result.push_back(s);
    }
    return result;
}

// Editör durumunu kaydetme fonksiyonu (Undo için)
void saveState(Editor& ed)
{
    EditorState state;
    state.lines = getEditorContent(ed);
    state.cursorRow = ed.currentRow;
    state.cursorCol = ed.currentCol;

    ed.undoStack.push_back(state);
}

// Editör durumunu geri yükleme fonksiyonu (Undo için)
void loadState(Editor& ed, const EditorState& state)
{
    // Eski satırları temizle
    for (Line* line : ed.lines) {
        Node* temp = line->head;
        while (temp) {
            Node* next = temp->next;
            delete temp;
            temp = next;
        }
        delete line;
    }

    ed.lines.clear();

    // Yeni durumu yükle
    for (const string& s : state.lines) {
        Line* newLine = new Line();

        for (char c : s) {
            Node* newNode = new Node(c);

            if (newLine->head == nullptr) {
                newLine->head = newLine->tail = newNode;
            }
            else {
                newNode->prev = newLine->tail;
                newLine->tail->next = newNode;
                newLine->tail = newNode;
            }

            newLine->lineLength++;
        }

        ed.lines.push_back(newLine);
    }

    if (ed.lines.empty()) {
        ed.lines.push_back(new Line());
    }

    ed.currentRow = state.cursorRow;
    ed.currentCol = state.cursorCol;

    ed.isSelecting = false;
    ed.anchorRow = -1;
    ed.anchorCol = -1;
}

// Undo işlemi fonksiyonu
void undo(Editor& ed)
{
    if (!ed.undoStack.empty()) {
        EditorState prevState = ed.undoStack.back();
        ed.undoStack.pop_back();
        loadState(ed, prevState);

        system("cls");
        drawToolbox();
    }
}

// Editörün tüm içeriğini tek bir string olarak alma fonksiyonu (Panoya kopyalamak için)
string getEditorText(Editor& ed)
{
    string result = "";

    for (int i = 0; i < ed.lines.size(); i++)
    {
        Node* temp = ed.lines[i]->head;

        while (temp)
        {
            result += temp->data;
            temp = temp->next;
        }

        if (i != ed.lines.size() - 1)
        {
            result += '\n';
        }
    }

    return result;
}

// Editör içeriğini dosyaya kaydetme fonksiyonu
bool saveToFile(Editor& ed, const string& filePath)
{
    ofstream outFile(filePath);

    if (!outFile.is_open()) return false;

    outFile << getEditorText(ed);
    outFile.close();
    return true;
}

// Dosyadan içeriği okuyup editöre yükleme fonksiyonu
string promptFilePath(const string& message)
{
    string path = "";
    char ch;

    gotoxy(0, 3);
    cout << "                                                                                                    ";
    gotoxy(0, 3);
    cout << message;

    while (true)
    {
        gotoxy(0, 4);
        cout << "Dosya adi/yolu:                                                                                     ";
        gotoxy(16, 4);
        cout << path;

        ch = _getch();

        if (ch == 13) // ENTER
        {
            if (!path.empty()) break;
        }
        else if (ch == 27) // ESC
        {
            return "";
        }
        else if (ch == 8) // BACKSPACE
        {
            if (!path.empty()) path.pop_back();
        }
        else if (ch >= 32 && ch <= 126)
        {
            path += ch;
        }
    }

    return path;
}

// Belirtilen dizindeki dosya ve klasörleri listeleyen fonksiyon
vector<FileItem> listDirectory(const string& currentPath)
{
    vector<FileItem> items;

    for (const auto& entry : fs::directory_iterator(currentPath))
    {
        FileItem item;
        item.name = entry.path().filename().string();
        item.fullPath = entry.path().string();
        item.isDirectory = entry.is_directory();
        items.push_back(item);
    }

    sort(items.begin(), items.end(), [](const FileItem& a, const FileItem& b)
        {
            if (a.isDirectory != b.isDirectory)
                return a.isDirectory > b.isDirectory; // klasörler üstte
            return a.name < b.name;
        });

    return items;
}

// Dosya yöneticisi arayüzünü çizen fonksiyon
void drawFileManager(const string& title, const string& currentPath,
    const vector<FileItem>& items, int selectedIndex,
    const string& typedName, bool isSaveMode)
{
    system("cls");
    cout << "==================== " << title << " ====================\n";
    cout << "Bulunulan dizin: " << currentPath << "\n";
    cout << "SOL OK: Ust dizin | SAG OK / ENTER: Klasore gir | ESC: Iptal\n";
    if (isSaveMode)
        cout << "Dosya adi yazabilirsiniz. ENTER ile tamamlanir.\n";
    else
        cout << "Dosya secip ENTER ile acabilirsiniz.\n";

    cout << "-----------------------------------------------------------\n";

    for (int i = 0; i < items.size(); i++)
    {
        if (i == selectedIndex) cout << "> ";
        else cout << "  ";

        if (items[i].isDirectory) cout << "[KLASOR] ";
        else cout << "[DOSYA ] ";

        cout << items[i].name << "\n";
    }

    if (isSaveMode)
    {
        cout << "\nYazilan dosya adi: " << typedName << "\n";
    }

    cout << "===========================================================\n";
}

// Dosya yöneticisi arayüzünü çalıştıran ve sonucu döndüren fonksiyon
string runFileManager(const string& title, bool isSaveMode)
{
    string currentPath = fs::current_path().string();
    string typedName = "";
    int selectedIndex = 0;

    while (true)
    {
        vector<FileItem> items = listDirectory(currentPath);

        if (selectedIndex >= (int)items.size())
            selectedIndex = max(0, (int)items.size() - 1);

        drawFileManager(title, currentPath, items, selectedIndex, typedName, isSaveMode);

        char ch = _getch();

        if (ch == 27) // ESC
        {
            system("cls");
            drawToolbox();
            return "";
        }
        else if (ch == 0 || ch == 224)
        {
            int special = _getch();  // char değil int kullan

            if (special == 72) // yukari
            {
                if (selectedIndex > 0) selectedIndex--;
            }
            else if (special == 80) // asagi
            {
                if (selectedIndex < (int)items.size() - 1) selectedIndex++;
            }
            else if (special == 75) // sol -> ust dizin
            {
                fs::path p(currentPath);
                if (p.has_parent_path())
                {
                    currentPath = p.parent_path().string();
                    selectedIndex = 0;
                }
            }
            else if (special == 77) // sag -> klasore gir
            {
                if (!items.empty() && items[selectedIndex].isDirectory)
                {
                    currentPath = items[selectedIndex].fullPath;
                    selectedIndex = 0;
                }
            }
        }
        else if (ch == 13) // ENTER
        {
            if (isSaveMode)
            {
                if (!typedName.empty())
                {
                    fs::path finalPath = fs::path(currentPath) / typedName;
                    system("cls");
                    drawToolbox();
                    return finalPath.string();
                }
                else if (!items.empty() && items[selectedIndex].isDirectory)
                {
                    currentPath = items[selectedIndex].fullPath;
                    selectedIndex = 0;
                }
            }
            else
            {
                if (!items.empty())
                {
                    if (items[selectedIndex].isDirectory)
                    {
                        currentPath = items[selectedIndex].fullPath;
                        selectedIndex = 0;
                    }
                    else
                    {
                        system("cls");
                        drawToolbox();
                        return items[selectedIndex].fullPath;
                    }
                }
            }
        }
        else if (isSaveMode)
        {
            if (ch == 8) // backspace
            {
                if (!typedName.empty()) typedName.pop_back();
            }
            else if (ch >= 32 && ch <= 126)
            {
                typedName += ch;
            }
        }
    }
}

int main()
{
    // Türkçe karakter ve konsol ayarları
    setlocale(LC_ALL, ".1254");
    SetConsoleOutputCP(1254); // Türkçe karakter çıkışı
    SetConsoleCP(1254);       // Türkçe karakter girişi

    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hInput, &mode);

    mode &= ~ENABLE_PROCESSED_INPUT;
    SetConsoleMode(hInput, mode);

    drawToolbox();

    Editor myEditor; // Editör nesnemizi oluşturuyoruz 

    char ch;
    bool running = true;

    string clipboard = "";
    string statusMessage = "";

    DWORD statusStartTime = 0;
    DWORD statusDuration = 700; // 0.7 saniye

    string currentFilePath = "";
    bool isModified = false;

    while (running)
    {
        if (!statusMessage.empty())
        {
            if (GetTickCount() - statusStartTime >= statusDuration)
            {
                statusMessage = "";
            }
        }
        // 2. Arayüzü Çiz
        renderEditor(myEditor, statusMessage);

        // 3. İmleci Konumlandır
        gotoxy(myEditor.currentCol + 4, myEditor.currentRow + 5);

        // 4. Tuş Bekle
        if (_kbhit())
        {
            ch = _getch();

            // 1. ADIM: Yön tuşları gibi özel tuşları (0 veya 224 ile başlar) yakala
            if (ch == 0 || (unsigned char)ch == 224)
            {
                ch = _getch(); // Tuşun gerçek kodunu (72, 75, 77, 80) oku

                switch (ch)
                {
                case 72: // YUKARI OK
                case 80: // AŞAĞI OK
                {
                    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool isCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

                    // 1. SEÇİM KONTROLÜ
                    if (isCtrl || isShift)
                    {
                        if (!myEditor.isSelecting || myEditor.anchorRow == -1)
                        {
                            myEditor.anchorRow = myEditor.currentRow;
                            myEditor.anchorCol = myEditor.currentCol;
                            myEditor.isSelecting = true;
                        }
                    }
                    else
                    {
                        // Modifikatör tuşlar bırakıldıysa seçimi kapat
                        myEditor.isSelecting = false;
                        myEditor.anchorRow = -1;
                        myEditor.anchorCol = -1;
                    }

                    // 2. HAREKET MANTIĞI
                    if (ch == 72 && myEditor.currentRow > 0) // YUKARI
                    {
                        myEditor.currentRow--;
                        if (myEditor.currentCol > myEditor.lines[myEditor.currentRow]->lineLength)
                        {
                            myEditor.currentCol = myEditor.lines[myEditor.currentRow]->lineLength;
                        }
                    }
                    else if (ch == 80 && myEditor.currentRow < (int)myEditor.lines.size() - 1) // AŞAĞI
                    {
                        myEditor.currentRow++;
                        if (myEditor.currentCol > myEditor.lines[myEditor.currentRow]->lineLength)
                        {
                            myEditor.currentCol = myEditor.lines[myEditor.currentRow]->lineLength;
                        }
                    }
                    break;
                }
                case 75: // SOL
                case 77: // SAĞ
                case 115: // CTRL+SOL
                case 116: // CTRL+SAĞ
                {
                    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool isCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0 || (ch == 115 || ch == 116);
                    // 1. SEÇİM DURUMU: CTRL veya SHIFT basılıysa seçimi başlat
                    if (isCtrl || isShift)
                    {
                        if (!myEditor.isSelecting || myEditor.anchorRow == -1)
                        {
                            myEditor.anchorRow = myEditor.currentRow;
                            myEditor.anchorCol = myEditor.currentCol;
                            myEditor.isSelecting = true;
                        }
                    }
                    else
                    {
                        myEditor.isSelecting = false;
                        myEditor.anchorRow = -1;
                        myEditor.anchorCol = -1;
                    }
                    bool moveLeft = (ch == 75 || ch == 115);

                    if (moveLeft) // SOLA HAREKET
                    {
                        // CTRL + SHIFT beraberse: KELİME KELİME ZIPLA
                        if (isCtrl && isShift)
                        {
                            if (myEditor.currentCol > 0)
                            {
                                while (myEditor.currentCol > 0)
                                {
                                    myEditor.currentCol--;
                                    Node* t = myEditor.lines[myEditor.currentRow]->head;
                                    for (int k = 0; k < myEditor.currentCol; k++) if (t) t = t->next;
                                    if (t && t->data == ' ') break;
                                }
                            }
                            else if (myEditor.currentRow > 0)
                            {
                                myEditor.currentRow--;
                                myEditor.currentCol = myEditor.lines[myEditor.currentRow]->lineLength;
                            }
                        }
                        // Sadece CTRL veya Sadece SHIFT: TEKER TEKER GİT
                        else
                        {
                            if (myEditor.currentCol > 0) myEditor.currentCol--;
                            else if (myEditor.currentRow > 0)
                            {
                                myEditor.currentRow--;
                                myEditor.currentCol = myEditor.lines[myEditor.currentRow]->lineLength;
                            }
                        }
                    }
                    else // SAĞA HAREKET
                    {
                        // CTRL + SHIFT beraberse: KELİME KELİME ZIPLA
                        if (isCtrl && isShift)
                        {
                            Line* curL = myEditor.lines[myEditor.currentRow];
                            while (myEditor.currentCol < curL->lineLength)
                            {
                                myEditor.currentCol++;
                                Node* t = curL->head;
                                for (int k = 0; k < myEditor.currentCol; k++) if (t) t = t->next;
                                if (t && t->data == ' ') break;
                            }
                        }
                        // Sadece CTRL veya Sadece SHIFT: TEKER TEKER GİT
                        else
                        {
                            if (myEditor.currentCol < myEditor.lines[myEditor.currentRow]->lineLength)
                                myEditor.currentCol++;
                            else if (myEditor.currentRow < (int)myEditor.lines.size() - 1)
                            {
                                myEditor.currentRow++;
                                myEditor.currentCol = 0;
                            }
                        }
                    }
                    break;
                }
                }
            }

            // 2. ADIM: Kontrol Karakterleri (ENTER, ESC, BACKSPACE vb.)
            // ENTER 
            else if (ch == 13)
            {
                saveState(myEditor);
                Line* curLine = myEditor.lines[myEditor.currentRow];
                Line* newLine = new Line();

                // 1. ADIM: Eğer imleç satırın sonu değilse, sağdaki karakterleri yeni satıra taşı
                if (myEditor.currentCol < curLine->lineLength)
                {
                    Node* temp = curLine->head;
                    for (int i = 0; i < myEditor.currentCol; i++)
                    {
                        temp = temp->next;
                    }

                    // Yeni satırın başlangıcı ve bitişini ayarla
                    newLine->head = temp;
                    newLine->tail = curLine->tail;
                    newLine->lineLength = curLine->lineLength - myEditor.currentCol;

                    // Eski satırı böl (bağlantıları kopar)
                    if (temp->prev)
                    {
                        temp->prev->next = nullptr;
                        curLine->tail = temp->prev;
                    }
                    else
                    {
                        // Eğer en baştayken enter basıldıysa eski satır boşalır
                        curLine->head = curLine->tail = nullptr;
                    }
                    temp->prev = nullptr;
                    curLine->lineLength = myEditor.currentCol;
                }
                // 2. ADIM: Yeni satırı editöre ekle
                myEditor.lines.insert(myEditor.lines.begin() + myEditor.currentRow + 1, newLine);
                myEditor.currentRow++;
                myEditor.currentCol = 0;
                isModified = true;
            }

            // BACKSPACE/CTRL+H/CTRL+Backspace

            else if (ch == 8 || ch == 127)
            {
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

                if (ctrl && (GetKeyState('H') & 0x8000))
                {
                    string findQ = "", replaceQ = "";
                    bool cancelled = false;

                    while (true)
                    {
                        gotoxy(0, 0);
                        searchAndHighlight(myEditor, findQ, -1, 95, 95);
                        renderEditor(myEditor, statusMessage);

                        gotoxy(0, 3);
                        cout << "ARANACAK:                                     ";
                        gotoxy(10, 3);
                        cout << findQ;

                        char subCh = _getch();

                        if (subCh == 13 && !findQ.empty()) break;
                        if (subCh == 27) { cancelled = true; break; }
                        if (subCh == 8) { if (!findQ.empty()) findQ.pop_back(); }
                        else if (subCh >= 32 && subCh <= 126) findQ += subCh;
                    }

                    if (!cancelled)
                    {
                        while (true)
                        {
                            gotoxy(0, 0);
                            renderEditor(myEditor, statusMessage);

                            gotoxy(0, 3); cout << "ARANACAK: " << findQ;
                            gotoxy(0, 4); cout << "YENI DEGER:                                   ";
                            gotoxy(12, 4);
                            cout << replaceQ;

                            char subCh = _getch();

                            if (subCh == 13)
                            {
                                vector<Match> matches = searchAndHighlight(myEditor, findQ, -1);

                                if (!matches.empty())
                                {
                                    saveState(myEditor);
                                }

                                for (int i = matches.size() - 1; i >= 0; i--)
                                {
                                    replaceText(myEditor, matches[i].row, matches[i].col, (int)findQ.length(), replaceQ);
                                }
                                isModified = true;
                                break;
                            }
                            if (subCh == 27) { cancelled = true; break; }
                            if (subCh == 8) { if (!replaceQ.empty()) replaceQ.pop_back(); }
                            else if (subCh >= 32 && subCh <= 126) replaceQ += subCh;
                        }
                    }
                    searchAndHighlight(myEditor, "", -1);
                    system("cls");
                    drawToolbox();
                    renderEditor(myEditor, statusMessage);
                }
                else if (ctrl || ch == 127)
                {
                    saveState(myEditor);
                    deleteWord(myEditor);
                    isModified = true;
                }
                else
                {
                    saveState(myEditor);
                    handleBackspace(myEditor);
                    isModified = true;
                }
            }

            // ESC (Çıkış)
            else if (ch == 27)
            {
                if (!isModified)
                {
                    running = false;
                }
                else
                {
                    while (true)
                    {
                        gotoxy(0, 3);
                        cout << "Kaydedilmemis degisiklikler var: [1] Kaydet  [2] Farkli Kaydet  [3] Kaydetmeden Cik   ";
                        gotoxy(0, 4);
                        cout << "Seciminizi yapin:                                                                        ";

                        char choice = _getch();

                        if (choice == '1') // Kaydet
                        {
                            if (currentFilePath.empty())
                            {
                                string newPath = runFileManager("Dosya Kaydet", true);

                                if (!newPath.empty())
                                {
                                    if (saveToFile(myEditor, newPath))
                                    {
                                        currentFilePath = newPath;
                                        isModified = false;
                                        statusMessage = "Dosya kaydedildi.";
                                        statusStartTime = GetTickCount();
                                        running = false;
                                        break;
                                    }
                                    else
                                    {
                                        statusMessage = "Dosya kaydedilemedi.";
                                        statusStartTime = GetTickCount();
                                    }
                                }
                                else
                                {
                                    break; // Kaydet ekranindan vazgecildi
                                }
                            }
                            else
                            {
                                if (saveToFile(myEditor, currentFilePath))
                                {
                                    isModified = false;
                                    statusMessage = "Dosya kaydedildi.";
                                    statusStartTime = GetTickCount();
                                    running = false;
                                    break;
                                }
                                else
                                {
                                    statusMessage = "Dosya kaydedilemedi.";
                                    statusStartTime = GetTickCount();
                                }
                            }
                        }
                        else if (choice == '2') // Farkli Kaydet
                        {
                            string newPath = runFileManager("Dosya Farkli Kaydet", true);

                            if (!newPath.empty())
                            {
                                if (saveToFile(myEditor, newPath))
                                {
                                    currentFilePath = newPath;
                                    isModified = false;
                                    statusMessage = "Dosya farkli kaydedildi.";
                                    statusStartTime = GetTickCount();
                                    running = false;
                                    break;
                                }
                                else
                                {
                                    statusMessage = "Dosya kaydedilemedi.";
                                    statusStartTime = GetTickCount();
                                }
                            }
                            else
                            {
                                break; // Farkli kaydetten vazgecildi
                            }
                        }
                        else if (choice == '3') // Kaydetmeden cik
                        {
                            running = false;
                            break;
                        }
                    }
                    system("cls");
                    drawToolbox();
                }
            }
            // 3. ADIM: Sadece yazdırılabilir karakterleri ekle
            else if (ch >= 32 && ch <= 126 || (unsigned char)ch > 127)
            {
                saveState(myEditor);
                insertCharacter(myEditor, ch);
                isModified = true;
            }
            // CTRL + C : Kopyala
            else if (ch == 3)
            {
                if (myEditor.isSelecting)
                {
                    clipboard = getSelectedText(myEditor);

                    if (!clipboard.empty())
                    {
                        copyToWindowsClipboard(clipboard);
                        statusMessage = "Secili metin kopyalandi.";
                        statusStartTime = GetTickCount();
                    }
                    else
                    {
                        statusMessage = "Kopyalanacak metin bulunamadi.";
                        statusStartTime = GetTickCount();
                    }
                }
                else
                {
                    statusMessage = "Once bir metin secmelisin.";
                    statusStartTime = GetTickCount();
                }
            }
            // CTRL + V : Yapistir
            else if (ch == 22)
            {
                statusMessage = "CTRL+V yakalandi.";
                statusStartTime = GetTickCount();
                string pastedText = getFromWindowsClipboard();

                if (!pastedText.empty())
                {
                    saveState(myEditor);
                    clipboard = pastedText;
                    pasteText(myEditor, pastedText);
                    isModified = true;
                }
            }
            // CTRL + X : Kes
            else if (ch == 24)
            {
                if (myEditor.isSelecting)
                {
                    clipboard = getSelectedText(myEditor);

                    if (!clipboard.empty())
                    {
                        saveState(myEditor);
                        copyToWindowsClipboard(clipboard);
                        deleteSelectedArea(myEditor);
                        isModified = true;

                        statusMessage = "Metin kesildi.";
                        statusStartTime = GetTickCount();
                    }
                    else
                    {
                        statusMessage = "Kesilecek metin yok.";
                        statusStartTime = GetTickCount();
                    }
                }
                else
                {
                    statusMessage = "Once bir metin secmelisin.";
                    statusStartTime = GetTickCount();
                }
            }
            // CTRL + Z : Geri Al
            else if (ch == 26)
            {
                if (!myEditor.undoStack.empty())
                {
                    undo(myEditor);
                    isModified = true;
                    statusMessage = "Geri alindi.";
                    statusStartTime = GetTickCount();
                }
                else
                {
                    statusMessage = "Geri alinacak islem yok.";
                    statusStartTime = GetTickCount();
                }
            }

            // CTRL + O : Dosya Ac
            else if (ch == 15)
            {
                string openPath = runFileManager("Dosya Ac", false);

                if (!openPath.empty())
                {
                    ifstream inFile(openPath);
                    if (inFile.is_open())
                    {
                        // Eski editor icerigini temizle
                        for (Line* line : myEditor.lines)
                        {
                            Node* temp = line->head;
                            while (temp)
                            {
                                Node* next = temp->next;
                                delete temp;
                                temp = next;
                            }
                            delete line;
                        }
                        myEditor.lines.clear();

                        string lineText;
                        while (getline(inFile, lineText))
                        {
                            Line* newLine = new Line();
                            for (char c : lineText)
                            {
                                Node* newNode = new Node(c);
                                if (newLine->head == nullptr)
                                {
                                    newLine->head = newLine->tail = newNode;
                                }
                                else
                                {
                                    newNode->prev = newLine->tail;
                                    newLine->tail->next = newNode;
                                    newLine->tail = newNode;
                                }
                                newLine->lineLength++;
                            }
                            myEditor.lines.push_back(newLine);
                        }

                        if (myEditor.lines.empty())
                        {
                            myEditor.lines.push_back(new Line());
                        }

                        myEditor.currentRow = 0;
                        myEditor.currentCol = 0;
                        myEditor.anchorRow = -1;
                        myEditor.anchorCol = -1;
                        myEditor.isSelecting = false;

                        currentFilePath = openPath;
                        isModified = false;

                        statusMessage = "Dosya acildi.";
                        statusStartTime = GetTickCount();
                    }
                }
            }

            // CTRL + S : Dosya Kaydet
            else if (ch == 19)
            {
                if (currentFilePath.empty())
                {
                    string savePath = runFileManager("Dosya Kaydet", true);

                    if (!savePath.empty())
                    {
                        if (saveToFile(myEditor, savePath))
                        {
                            currentFilePath = savePath;
                            isModified = false;
                            statusMessage = "Dosya kaydedildi.";
                            statusStartTime = GetTickCount();
                        }
                        else
                        {
                            statusMessage = "Dosya kaydedilemedi.";
                            statusStartTime = GetTickCount();
                        }
                    }
                }
                else
                {
                    if (saveToFile(myEditor, currentFilePath))
                    {
                        isModified = false;
                        statusMessage = "Dosya kaydedildi.";
                        statusStartTime = GetTickCount();
                    }
                    else
                    {
                        statusMessage = "Dosya kaydedilemedi.";
                        statusStartTime = GetTickCount();
                    }
                }
            }

            // CTRL + F ve CTRL + G 
            else if (ch == 6)
            {
                string query = "";
                int activeIndex = 0;
                vector<Match> matches;

                while (true)
                {
                    gotoxy(0, 3);
                    cout << "Aranacak: " << query << " [Bulunan: " << matches.size() << "]      ";
                    gotoxy(10 + query.length(), 3);
                    char subCh = _getch();

                    if (subCh == 27)
                    {
                        searchAndHighlight(myEditor, "", -1); // Boş sorgu göndererek tüm renkleri 7 (beyaz) yapar
                        break;
                    }

                    // 1. DURUM: CTRL + G (ASCII 7) - Sonraki eşleşmeye git
                    else if (subCh == 7)
                    {
                        if (!matches.empty())
                        {
                            activeIndex = (activeIndex + 1) % matches.size();
                        }
                    }

                    // 2. DURUM: OK TUŞLARI (Özel Karakter Girişi) 
                    else if (subCh == -32 || subCh == 224 || subCh == 0)
                    {
                        char arrow = _getch(); // İkinci kodu oku
                        if (!matches.empty())
                        {
                            if (arrow == 77 || arrow == 80) // SAĞ veya AŞAĞI: Sonraki
                                activeIndex = (activeIndex + 1) % matches.size();
                            else if (arrow == 75 || arrow == 72) // SOL veya YUKARI: Önceki
                                activeIndex = (activeIndex - 1 + matches.size()) % matches.size();
                        }
                    }

                    // 3. DURUM: BACKSPACE (Aramayı düzenle)
                    else if (subCh == 8)
                    {
                        if (!query.empty()) query.pop_back();
                        activeIndex = 0; // Arama değişince başa dön
                    }

                    // 4. DURUM: YAZI YAZMA
                    else if (subCh >= 32 && subCh <= 126)
                    {
                        query += subCh;
                        activeIndex = 0;
                    }
                    // Aramayı güncelle ve renklendir
                    matches = searchAndHighlight(myEditor, query, activeIndex);

                    // İmleci ve ekranı güncelle
                    if (!matches.empty())
                    {
                        myEditor.currentRow = matches[activeIndex].row;
                        myEditor.currentCol = matches[activeIndex].col;
                    }
                    renderEditor(myEditor, statusMessage);
                    // Yazmaya devam etmek için imleci arama satırına geri getir
                    gotoxy(10 + query.length(), 3);
                }
                // Arama modundan çıkınca temizlik yap
                system("cls");
                drawToolbox();
            }
        }// İşlemciyi yormamak için çok kısa bir bekleme
        Sleep(10);
    }
    return 0;
}