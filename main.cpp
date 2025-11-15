// #define CPPHTTPLIB_OPENSSL_SUPPORT  // Comentado - no necesitamos SSL para servidor local
#include "httplib.h"
#include "json.hpp"
#include <windows.h>
#include <setupapi.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>

using json = nlohmann::json;

// Clase para manejar la impresora
class ESCPOSPrinter {
private:
    HANDLE hPrinter;
    std::string printerName;
    bool isOpen;

    // Comandos ESC/POS
    const std::vector<BYTE> ESC_INIT = {0x1B, 0x40};
    const std::vector<BYTE> ESC_ALIGN_LEFT = {0x1B, 0x61, 0x00};
    const std::vector<BYTE> ESC_ALIGN_CENTER = {0x1B, 0x61, 0x01};
    const std::vector<BYTE> ESC_ALIGN_RIGHT = {0x1B, 0x61, 0x02};
    const std::vector<BYTE> ESC_FONT_A = {0x1B, 0x4D, 0x00};
    const std::vector<BYTE> ESC_FEED = {0x0A};
    const std::vector<BYTE> ESC_CUT = {0x1D, 0x56, 0x00};

public:
    ESCPOSPrinter() : hPrinter(NULL), isOpen(false) {}

    ~ESCPOSPrinter() {
        close();
    }

    // Listar impresoras disponibles
    static std::vector<std::string> listPrinters() {
        std::vector<std::string> printers;
        DWORD dwNeeded, dwReturned;
        
        EnumPrinters(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, 
                     NULL, 2, NULL, 0, &dwNeeded, &dwReturned);
        
        if (dwNeeded > 0) {
            std::vector<BYTE> buffer(dwNeeded);
            PRINTER_INFO_2* pPrinterInfo = (PRINTER_INFO_2*)buffer.data();
            
            if (EnumPrinters(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                           NULL, 2, buffer.data(), dwNeeded, &dwNeeded, &dwReturned)) {
                for (DWORD i = 0; i < dwReturned; i++) {
                    printers.push_back(pPrinterInfo[i].pPrinterName);
                }
            }
        }
        
        return printers;
    }

    // Abrir conexión con la impresora
    bool open(const std::string& name = "") {
        if (isOpen) return true;

        if (name.empty()) {
            // Buscar primera impresora disponible
            auto printers = listPrinters();
            if (printers.empty()) {
                std::cerr << "No se encontraron impresoras" << std::endl;
                return false;
            }
            printerName = printers[0];
        } else {
            printerName = name;
        }

        PRINTER_DEFAULTS pd = {NULL, NULL, PRINTER_ACCESS_USE};
        if (!OpenPrinter((LPSTR)printerName.c_str(), &hPrinter, &pd)) {
            std::cerr << "Error abriendo impresora: " << GetLastError() << std::endl;
            return false;
        }

        isOpen = true;
        std::cout << "🖨️ Impresora abierta: " << printerName << std::endl;
        return true;
    }

    void close() {
        if (isOpen && hPrinter) {
            ClosePrinter(hPrinter);
            hPrinter = NULL;
            isOpen = false;
        }
    }

    // Enviar datos raw a la impresora
    bool sendRaw(const std::vector<BYTE>& data) {
        if (!isOpen) {
            std::cerr << "Impresora no está abierta" << std::endl;
            return false;
        }

        DOC_INFO_1 docInfo;
        docInfo.pDocName = (LPSTR)"Print Job";
        docInfo.pOutputFile = NULL;
        docInfo.pDatatype = (LPSTR)"RAW";

        DWORD dwJob = StartDocPrinter(hPrinter, 1, (LPBYTE)&docInfo);
        if (dwJob == 0) {
            std::cerr << "Error en StartDocPrinter: " << GetLastError() << std::endl;
            return false;
        }

        if (!StartPagePrinter(hPrinter)) {
            std::cerr << "Error en StartPagePrinter: " << GetLastError() << std::endl;
            EndDocPrinter(hPrinter);
            return false;
        }

        DWORD dwBytesWritten;
        if (!WritePrinter(hPrinter, (LPVOID)data.data(), data.size(), &dwBytesWritten)) {
            std::cerr << "Error en WritePrinter: " << GetLastError() << std::endl;
            EndPagePrinter(hPrinter);
            EndDocPrinter(hPrinter);
            return false;
        }

        EndPagePrinter(hPrinter);
        EndDocPrinter(hPrinter);
        
        return true;
    }

    // Imprimir texto simple
    bool printText(const std::string& text) {
        std::vector<BYTE> data(text.begin(), text.end());
        data.push_back(0x0A); // Line feed
        return sendRaw(data);
    }

    // Imprimir ticket completo
    bool printTicket(const std::vector<std::string>& lines) {
        if (!open()) return false;

        std::vector<BYTE> data;
        
        // Inicializar
        data.insert(data.end(), ESC_INIT.begin(), ESC_INIT.end());
        data.insert(data.end(), ESC_FONT_A.begin(), ESC_FONT_A.end());
        data.insert(data.end(), ESC_ALIGN_LEFT.begin(), ESC_ALIGN_LEFT.end());

        // Agregar líneas
        for (const auto& line : lines) {
            data.insert(data.end(), line.begin(), line.end());
            data.insert(data.end(), ESC_FEED.begin(), ESC_FEED.end());
        }

        // Feed y corte
        data.insert(data.end(), ESC_FEED.begin(), ESC_FEED.end());
        data.insert(data.end(), ESC_FEED.begin(), ESC_FEED.end());
        data.insert(data.end(), ESC_CUT.begin(), ESC_CUT.end());

        return sendRaw(data);
    }

    // Calcular dígito verificador EAN13
    static int calculateEAN13CheckDigit(const std::string& code) {
        std::string base = code.substr(0, 12);
        while (base.length() < 12) base = "0" + base;
        
        int sum = 0;
        for (int i = 0; i < 12; i++) {
            int digit = base[i] - '0';
            sum += digit * (i % 2 == 0 ? 1 : 3);
        }
        return (10 - (sum % 10)) % 10;
    }

    // Obtener código EAN13 completo
    static std::string getFullEAN13(const std::string& code) {
        std::string base = code.substr(0, 12);
        while (base.length() < 12) base = "0" + base;
        // Nota: En tu código original comentaste el checkdigit, lo mantengo así
        return base;
    }

    // Imprimir código de barras EAN13
    bool printBarcode(const std::vector<std::string>& codes, int copies = 1, 
                     const std::string& text = "") {
        if (!open()) return false;

        std::vector<BYTE> data;
        
        // Inicializar
        data.insert(data.end(), ESC_INIT.begin(), ESC_INIT.end());
        data.insert(data.end(), ESC_ALIGN_CENTER.begin(), ESC_ALIGN_CENTER.end());

        for (int copy = 0; copy < copies; copy++) {
            // Texto opcional
            if (!text.empty()) {
                data.insert(data.end(), text.begin(), text.end());
                data.insert(data.end(), ESC_FEED.begin(), ESC_FEED.end());
            }

            // Imprimir códigos (dos por línea)
            for (size_t i = 0; i < codes.size(); i += 2) {
                std::string code1 = getFullEAN13(codes[i]);
                
                // Comando de código de barras EAN13
                // GS k m n d1...dn (m=67 para EAN13, n=12 dígitos)
                std::vector<BYTE> barcode = {0x1D, 0x6B, 0x43, 0x0C};
                barcode.insert(barcode.end(), code1.begin(), code1.end());
                data.insert(data.end(), barcode.begin(), barcode.end());
                
                // Configurar altura del código de barras
                std::vector<BYTE> height = {0x1D, 0x68, 0x50}; // Altura 80
                data.insert(data.end(), height.begin(), height.end());
                
                data.insert(data.end(), ESC_FEED.begin(), ESC_FEED.end());

                // Si hay un segundo código en el par
                if (i + 1 < codes.size()) {
                    std::string code2 = getFullEAN13(codes[i + 1]);
                    std::vector<BYTE> barcode2 = {0x1D, 0x6B, 0x43, 0x0C};
                    barcode2.insert(barcode2.end(), code2.begin(), code2.end());
                    data.insert(data.end(), barcode2.begin(), barcode2.end());
                    data.insert(data.end(), ESC_FEED.begin(), ESC_FEED.end());
                }
                
                data.insert(data.end(), ESC_FEED.begin(), ESC_FEED.end());
            }
        }

        // Corte
        data.insert(data.end(), ESC_CUT.begin(), ESC_CUT.end());

        std::cout << "📦 " << codes.size() << " código(s) impresos × " 
                  << copies << " copia(s)" << std::endl;

        return sendRaw(data);
    }

    bool getIsOpen() const { return isOpen; }
    std::string getPrinterName() const { return printerName; }
};

// Instancia global de la impresora
ESCPOSPrinter printer;

int main() {
    using namespace httplib;

    Server svr;

    // Middleware CORS para todas las respuestas
    svr.set_pre_routing_handler([](const Request& req, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.set_header("Access-Control-Max-Age", "3600");
        
        // Si es una petición OPTIONS, responder inmediatamente
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // GET /ping - Health check
    svr.Get("/ping", [](const Request&, Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        
        json response;
        response["status"] = "ok";
        
        if (printer.getIsOpen()) {
            response["message"] = "🖨️ Print Agent activo (impresora conectada)";
            response["printer"] = printer.getPrinterName();
        } else {
            response["message"] = "⚠️ Print Agent activo (sin impresora detectada)";
        }
        
        res.set_content(response.dump(), "application/json");
    });

    // GET /printers - Listar impresoras
    svr.Get("/printers", [](const Request&, Response& res) {
        auto printers = ESCPOSPrinter::listPrinters();
        
        json response;
        response["printers"] = printers;
        
        res.set_content(response.dump(), "application/json");
    });

    // POST /print/ticket - Imprimir ticket
    svr.Post("/print/ticket", [](const Request& req, Response& res) {
        try {
            auto body = json::parse(req.body);
            
            if (!body.contains("lines") || !body["lines"].is_array()) {
                json error;
                error["error"] = "El body debe contener un array 'lines'";
                res.status = 400;
                res.set_content(error.dump(), "application/json");
                return;
            }

            std::vector<std::string> lines = body["lines"].get<std::vector<std::string>>();

            if (printer.printTicket(lines)) {
                json response;
                response["success"] = true;
                std::cout << "🧾 Ticket impreso correctamente" << std::endl;
                res.set_content(response.dump(), "application/json");
            } else {
                json error;
                error["error"] = "Error al imprimir ticket";
                res.status = 500;
                res.set_content(error.dump(), "application/json");
            }
        } catch (const std::exception& e) {
            json error;
            error["error"] = e.what();
            res.status = 500;
            res.set_content(error.dump(), "application/json");
        }
    });

    // POST /print/barcode - Imprimir código de barras
    svr.Post("/print/barcode", [](const Request& req, Response& res) {
        try {
            auto body = json::parse(req.body);
            
            std::vector<std::string> codes;
            
            // Códigos puede ser string o array
            if (body.contains("codes")) {
                if (body["codes"].is_array()) {
                    codes = body["codes"].get<std::vector<std::string>>();
                } else if (body["codes"].is_string()) {
                    codes.push_back(body["codes"].get<std::string>());
                }
            }

            if (codes.empty()) {
                json error;
                error["error"] = "Debe enviar al menos un código";
                res.status = 400;
                res.set_content(error.dump(), "application/json");
                return;
            }

            int copies = body.value("copies", 1);
            std::string text = body.value("text", "");

            if (printer.printBarcode(codes, copies, text)) {
                json response;
                response["success"] = true;
                response["codes"] = codes;
                response["copies"] = copies;
                res.set_content(response.dump(), "application/json");
            } else {
                json error;
                error["error"] = "Error al imprimir códigos";
                res.status = 500;
                res.set_content(error.dump(), "application/json");
            }
        } catch (const std::exception& e) {
            json error;
            error["error"] = e.what();
            res.status = 500;
            res.set_content(error.dump(), "application/json");
        }
    });

    // Intentar abrir impresora al inicio
    printer.open();

    std::cout << "🖨️ Print Agent escuchando en http://localhost:9999" << std::endl;
    std::cout << "Presiona Ctrl+C para detener el servidor..." << std::endl;

    svr.listen("0.0.0.0", 9999);

    return 0;
}