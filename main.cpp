// ============================================================================
// HIVA Sistemas de Impresión - PrintAgent v1.0
// Servicio local de impresión ESC/POS + API HTTP
// ============================================================================

#include "httplib.h"
#include "json.hpp"
#include <windows.h>
#include <setupapi.h>
#include <iostream>
#include <vector>
#include <string>

using json = nlohmann::json;

// ============================================================================
// Clase ESCPOSPrinter - Manejo de impresoras térmicas ESC/POS
// ============================================================================

class ESCPOSPrinter {
private:
    HANDLE hPrinter;
    std::string printerName;
    bool isOpen;

    const std::vector<BYTE> ESC_INIT        = {0x1B, 0x40};
    const std::vector<BYTE> ESC_ALIGN_LEFT  = {0x1B, 0x61, 0x00};
    const std::vector<BYTE> ESC_ALIGN_CENTER= {0x1B, 0x61, 0x01};
    const std::vector<BYTE> ESC_FEED        = {0x0A};
    const std::vector<BYTE> ESC_CUT         = {0x1D, 0x56, 0x00};

public:
    ESCPOSPrinter() : hPrinter(NULL), isOpen(false) {}
    ~ESCPOSPrinter() { close(); }

    static std::vector<std::string> listPrinters() {
        std::vector<std::string> printers;
        DWORD needed = 0, returned = 0;

        EnumPrinters(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                     NULL, 2, NULL, 0, &needed, &returned);

        if (needed == 0) return printers;

        std::vector<BYTE> buffer(needed);
        PRINTER_INFO_2* info = (PRINTER_INFO_2*)buffer.data();

        if (EnumPrinters(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                         NULL, 2, buffer.data(), needed, &needed, &returned))
        {
            for (DWORD i = 0; i < returned; i++)
                printers.emplace_back(info[i].pPrinterName);
        }

        return printers;
    }

    bool open(const std::string& name = "") {
        if (isOpen) return true;

        if (name.empty()) {
            auto printers = listPrinters();
            if (printers.empty()) {
                std::cerr << "[HIVA] No se encontraron impresoras instaladas.\n";
                return false;
            }
            printerName = printers[0];
        } else {
            printerName = name;
        }

        PRINTER_DEFAULTS pd = {NULL, NULL, PRINTER_ACCESS_USE};
        if (!OpenPrinter((LPSTR)printerName.c_str(), &hPrinter, &pd)) {
            std::cerr << "[HIVA] Error al abrir impresora. Codigo: " << GetLastError() << "\n";
            return false;
        }

        isOpen = true;
        return true;
    }

    void close() {
        if (isOpen && hPrinter) {
            ClosePrinter(hPrinter);
            hPrinter = NULL;
            isOpen = false;
        }
    }

    bool sendRaw(const std::vector<BYTE>& data) {
        if (!isOpen) return false;

        DOC_INFO_1 doc;
        doc.pDocName   = (LPSTR)"HIVA Print Job";
        doc.pOutputFile= NULL;
        doc.pDatatype  = (LPSTR)"RAW";

        DWORD job = StartDocPrinter(hPrinter, 1, (LPBYTE)&doc);
        if (job == 0) return false;

        if (!StartPagePrinter(hPrinter)) {
            EndDocPrinter(hPrinter);
            return false;
        }

        DWORD written;
        BOOL ok = WritePrinter(hPrinter, (LPVOID)data.data(), data.size(), &written);

        EndPagePrinter(hPrinter);
        EndDocPrinter(hPrinter);
        return ok;
    }

    bool printTicket(const std::vector<std::string>& lines) {
        if (!open()) return false;

        std::vector<BYTE> data;
        data.insert(data.end(), ESC_INIT.begin(), ESC_INIT.end());
        data.insert(data.end(), ESC_ALIGN_LEFT.begin(), ESC_ALIGN_LEFT.end());

        for (auto& line : lines) {
            data.insert(data.end(), line.begin(), line.end());
            data.push_back(0x0A);
        }

        data.insert(data.end(), ESC_CUT.begin(), ESC_CUT.end());
        return sendRaw(data);
    }

    bool printBarcode(const std::vector<std::string>& codes,
                      int copies, const std::string& text)
    {
        if (!open()) return false;

        std::vector<BYTE> data;
        data.insert(data.end(), ESC_INIT.begin(), ESC_INIT.end());
        data.insert(data.end(), ESC_ALIGN_CENTER.begin(), ESC_ALIGN_CENTER.end());

        for (int c = 0; c < copies; c++) {
            if (!text.empty()) {
                data.insert(data.end(), text.begin(), text.end());
                data.push_back(0x0A);
            }

            for (auto& code : codes) {
                std::vector<BYTE> bc = {0x1D, 0x6B, 0x43, 0x0C};
                bc.insert(bc.end(), code.begin(), code.end());
                data.insert(data.end(), bc.begin(), bc.end());
                data.push_back(0x0A);
            }
        }

        data.insert(data.end(), ESC_CUT.begin(), ESC_CUT.end());
        return sendRaw(data);
    }

    bool getIsOpen() const { return isOpen; }
    std::string getPrinterName() const { return printerName; }
};

// ============================================================================
// API HTTP
// ============================================================================

ESCPOSPrinter printer;

int main() {
    // Consola limpia sin caracteres raros
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    using namespace httplib;
    Server svr;

    // CORS
    svr.set_pre_routing_handler([](const Request& req, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");

        if (req.method == "OPTIONS") {
            res.status = 204;
            return Server::HandlerResponse::Handled;
        }
        return Server::HandlerResponse::Unhandled;
    });

    // Health check
    svr.Get("/ping", [](const Request&, Response& res) {
        json j;
        j["service"] = "HIVA PrintAgent";
        j["printer"] = printer.getPrinterName();
        j["status"]  = printer.getIsOpen() ? "online" : "no_printer";
        res.set_content(j.dump(), "application/json");
    });

    // Lista impresoras
    svr.Get("/printers", [](const Request&, Response& res) {
        json j;
        j["printers"] = ESCPOSPrinter::listPrinters();
        res.set_content(j.dump(), "application/json");
    });

    // Ticket
    svr.Post("/print/ticket", [](const Request& req, Response& res) {
        auto body = json::parse(req.body);
        auto lines = body["lines"].get<std::vector<std::string>>();
        bool ok = printer.printTicket(lines);
        res.set_content(ok ? "{\"success\":true}" : "{\"success\":false}", "application/json");
    });

    // Barcode
    svr.Post("/print/barcode", [](const Request& req, Response& res) {
        auto body = json::parse(req.body);
        auto codes = body["codes"].get<std::vector<std::string>>();
        int copies = body.value("copies", 1);
        std::string text = body.value("text", "");
        bool ok = printer.printBarcode(codes, copies, text);
        res.set_content(ok ? "{\"success\":true}" : "{\"success\":false}", "application/json");
    });

    // Banner profesional limpio
    std::cout << "-----------------------------------------------\n";
    std::cout << " HIVA Sistemas de Impresion - PrintAgent v1.0\n";
    std::cout << " Servicio local de impresion ESC/POS\n";
    std::cout << "-----------------------------------------------\n";

    if (printer.open())
        std::cout << " Impresora activa: " << printer.getPrinterName() << "\n";
    else
        std::cout << " Sin impresora disponible\n";

    std::cout << " MANTENE LA ABIERTA LA VENTANA";

    svr.listen("0.0.0.0", 9999);
    return 0;
}
