// HOST-SLIM-001 F4: regressão sem jogo; erros continuam ativos em Release (sem assert/NDEBUG).
#include "soh/modassets/ModAssetAliasPlan.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/ResourceFactory.h"
#include "ship/utils/StrHash64.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace {
using namespace SohModAssets;
using Files = std::unordered_map<std::string, std::string>;
void Require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string Bytes(const std::shared_ptr<Ship::File>& file) {
    return file && file->Buffer ? std::string(file->Buffer->begin(), file->Buffer->end()) : "";
}
class RamArchive final : public Ship::Archive {
  public:
    Files files;
    std::atomic<int> closes{ 0 };
    std::function<void()> onRead;
    RamArchive(std::string path, Files data) : Ship::Archive(path), files(std::move(data)) {
    }
    bool Open() override {
        for (const auto& [name, bytes] : files)
            IndexFile(name);
        return true;
    }
    bool Close() override {
        ++closes;
        return true;
    }
    bool WriteFile(const std::string&, const std::vector<uint8_t>&) override {
        return false;
    }
    std::shared_ptr<Ship::File> LoadFile(const std::string& name) override {
        auto found = files.find(name);
        if (found == files.end())
            return nullptr;
        if (onRead)
            onRead();
        auto file = std::make_shared<Ship::File>();
        file->Buffer = std::make_shared<std::vector<char>>(found->second.begin(), found->second.end());
        file->IsLoaded = true;
        return file;
    }
    std::shared_ptr<Ship::File> LoadFile(uint64_t hash) override {
        const auto names = ListFiles();
        const auto found = names->find(hash);
        return found == names->end() ? nullptr : LoadFile(found->second);
    }
};
struct CacheProbe : Ship::ResourceManager {
    using Ship::ResourceManager::CheckCache;
};
struct Fixture {
    CacheProbe rm;
    std::shared_ptr<Ship::ArchiveManager> manager;
    Archives privateArchives;
    std::unordered_map<Ship::Archive*, std::shared_ptr<RamArchive>> backends;
    Fixture() {
        rm.Init({}, {}, static_cast<int32_t>(std::thread::hardware_concurrency()));
        manager = rm.GetArchiveManager();
    }
    std::shared_ptr<Ship::Archive> Add(std::string stem, Files files, std::string prefix = "") {
        auto backend = std::make_shared<RamArchive>(stem, std::move(files));
        Hashes reserved;
        // O vetor de GetArchives() vive só no shared_ptr: segurá-lo antes do laço (sem isso, UB no MSVC).
        const auto mounted = manager->GetArchives();
        for (const auto& archive : *mounted)
            for (const auto& [hash, name] : *archive->ListFiles())
                reserved.insert(hash);
        auto wrapper = std::make_shared<ModAssetArchive>(stem, prefix.empty() ? "mod/" + stem + "/" : prefix, reserved,
                                                         false, backend);
        wrapper->Load();
        auto managed = manager->AddArchive(wrapper);
        Require(managed != nullptr, "montagem inicial falhou");
        backends[managed.get()] = backend;
        privateArchives.push_back(managed);
        return managed;
    }
    std::shared_ptr<ModAssetArchive> ReopenRam(const ModAssetArchive& wrapper, const Hashes& aliases) {
        auto& source = const_cast<ModAssetArchive&>(wrapper);
        auto backend = std::make_shared<RamArchive>(source.GetPath(), backends.at(&source)->files);
        auto next = std::make_shared<ModAssetArchive>(source.GetPath(), wrapper.Prefix(), aliases, true, backend);
        next->Load();
        backends[next.get()] = backend;
        return next;
    }
    AliasPlan Plan(const Ship::ArchiveManager::ArchiveSnapshot& snapshot, const Archives& rejected) {
        return PlanAliases(snapshot.archives, privateArchives, rejected,
                           [this](const auto& archive, const auto& aliases) { return ReopenRam(archive, aliases); });
    }
};
class TextResource final : public Ship::IResource {
  public:
    std::string text;
    TextResource(std::string value, std::shared_ptr<Ship::ResourceInitData> init = nullptr)
        : Ship::IResource(init ? init : std::make_shared<Ship::ResourceInitData>()), text(std::move(value)) {
    }
    void* GetRawPointer() override {
        return text.data();
    }
    size_t GetPointerSize() override {
        return text.size();
    }
};
class TextFactory final : public Ship::ResourceFactory {
  public:
    std::function<void()> beforeReturn;
    bool returnNull = false;
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> init) override {
        const auto bytes = Bytes(file);
        if (beforeReturn)
            beforeReturn();
        return returnNull ? nullptr : std::make_shared<TextResource>(bytes, init);
    }

  protected:
    bool FileHasValidFormatAndReader(std::shared_ptr<Ship::File>, std::shared_ptr<Ship::ResourceInitData>) override {
        return true;
    }
};
std::shared_ptr<Ship::ResourceInitData> InitData() {
    auto init = std::make_shared<Ship::ResourceInitData>();
    init->Format = RESOURCE_FORMAT_JSON; // Fábrica de teste lê bytes; dispensa parser e cabeçalho binário.
    init->Type = 0x1234abcd;
    init->ResourceVersion = 1;
    return init;
}
std::shared_ptr<TextFactory> Register(Ship::ResourceManager& rm) {
    auto factory = std::make_shared<TextFactory>();
    Require(rm.GetResourceLoader()->RegisterResourceFactory(factory, RESOURCE_FORMAT_JSON, "AliasTest", 0x1234abcd, 1),
            "registro da fabrica falhou");
    return factory;
}

void ResidualAlias() {
    Fixture f;
    auto a = f.Add("a", { { "objects/shared", "A" }, { "objects/shared.meta", "meta A" } });
    auto b = f.Add("b", { { "objects/shared", "B" }, { "objects/shared.meta", "meta B" }, { "audio/foreign", "no" } });
    Require(Bytes(f.manager->LoadFile("objects/shared")) == "A", "RED inicial precisa resolver A");
    Require(!b->HasFile("objects/shared"), "B deveria estar bloqueado antes do aceite");
    auto expected = f.manager->GetArchiveSnapshot();
    auto unchanged = f.Plan(expected, {});
    Require(unchanged.replacements == 0, "sem rejeicao deve manter wrappers");
    auto plan = f.Plan(expected, { a });
    Require(plan.replacements == 1, "B deve ganhar wrapper independente");
    Require(!b->HasFile("objects/shared"), "indice antigo de B foi mutado");
    f.rm.CacheExternalResource("objects/shared", std::make_shared<TextResource>("stale"));
    Archives published;
    Require(f.rm.PublishArchiveBatch(expected, plan.desired, plan.affected, published), "publicacao falhou");
    Require(Bytes(f.manager->LoadFile("objects/shared")) == "B", "V1: alias residual nao foi transferido A para B");
    Require(Bytes(f.manager->LoadFile(CRC64("objects/shared"))) == "B", "hash e nome divergiram");
    Require(Bytes(f.manager->LoadFile("objects/shared.meta")) == "meta B", "sidecar nao transferido");
    Require(!f.manager->HasFile("mod/a/objects/shared"), "privada rejeitada permaneceu");
    Require(f.manager->HasFile("mod/b/audio/foreign") && !f.manager->HasFile("audio/foreign"), "audio ganhou alias");
    Require(f.manager->LoadFileFromAllLayers("objects/shared").size() == 1, "camadas divergiram");
    Require(f.manager->ListFiles()->size() == published.front()->ListFiles()->size(), "listagem divergente");
    Require(!f.rm.GetCachedResource("objects/shared", true), "cache A permaneceu");
    Require(Bytes(a->LoadFile("objects/shared")) == "A" && f.backends.at(a.get())->closes == 0,
            "snapshot antigo fechado antes do leitor");
    std::cout << "PASS V1: A rejeitado, alias de B, privado, hash, sidecar, camadas, snapshot imutavel\n";
}
void NormalAndFirstWinner() {
    Fixture f;
    auto root = std::make_shared<RamArchive>("root", Files{ { "objects/shared", "root" } });
    root->Load();
    Require(f.manager->AddArchive(root) != nullptr, "raiz falhou");
    auto a = f.Add("a", { { "objects/free", "A" } });
    auto b = f.Add("b", { { "objects/shared", "B" }, { "objects/free", "B" } });
    f.Add("c", { { "objects/shared", "C" }, { "objects/free", "C" } });
    auto expected = f.manager->GetArchiveSnapshot();
    auto plan = f.Plan(expected, { a });
    Archives published;
    Require(f.rm.PublishArchiveBatch(expected, plan.desired, plan.affected, published), "publicacao precedencia");
    Require(root->IsLoaded() && root->closes == 0, "V2: transacao fechou um archive retido");
    Require(Bytes(f.manager->LoadFile("objects/shared")) == "root", "alias sobrepos raiz");
    Require(Bytes(f.manager->LoadFile("objects/free")) == "B", "primeiro sobrevivente deveria ganhar");
    std::cout << "PASS: precedencia da raiz e primeiro sobrevivente\n";
}
void ConflictsAndFailure() {
    Fixture f;
    auto a = f.Add("a", { { "objects/shared", "A" } });
    auto b = f.Add("a", { { "objects/shared", "B" } }, "mod/a/"); // mesmo stem privado
    auto expected = f.manager->GetArchiveSnapshot();
    auto plan = f.Plan(expected, { a });
    auto extra = f.Add("extra", { { "textures/extra", "X" } });
    Archives published;
    Require(!f.rm.PublishArchiveBatch(expected, plan.desired, plan.affected, published), "snapshot obsoleto aceito");
    Require(published.empty() && f.manager->HasFile("mod/extra/textures/extra"),
            "conflito perdeu montagem concorrente");
    auto current = f.manager->GetArchiveSnapshot();
    auto bad = std::make_shared<RamArchive>("bad", Files{});
    Require(!f.manager->ReplaceArchiveBatch(current, { bad }, published), "candidato fechado aceito");
    Require(f.manager->GetArchives()->size() == current.archives.size(), "falha publicou lote parcial");
    const auto beforeFiles = *f.manager->ListFiles();
    auto cached = std::make_shared<TextResource>("preservar");
    f.rm.CacheExternalResource("objects/shared", cached);
    std::shared_ptr<ModAssetArchive> reopened;
    Hashes requestedAliases;
    bool failed = false;
    try {
        PlanAliases(current.archives, f.privateArchives, { a }, [&](const auto& source, const auto& aliases) {
            // Muda apenas o namespace privado: caminho, prefixo e arquivos dos aliases são preservados.
            auto& view = const_cast<ModAssetArchive&>(source);
            auto files = f.backends.at(&view)->files;
            files.emplace("private/new", "D");
            auto changed = std::make_shared<RamArchive>(view.GetPath(), std::move(files));
            requestedAliases = aliases;
            reopened = std::make_shared<ModAssetArchive>(view.GetPath(), source.Prefix(), aliases, true, changed);
            reopened->Load();
            return reopened;
        });
    } catch (const std::runtime_error&) { failed = true; }
    // Fora do catch: um candidato inválido não pode satisfazer a recusa esperada por outro motivo.
    Require(reopened && reopened->IsLoaded(), "candidato reaberto precisa estar carregado");
    Require(reopened->Aliases() == requestedAliases && !requestedAliases.empty(),
            "aliases reabertos divergiram do plano");
    Require(reopened->GetPath() == b->GetPath() &&
                reopened->Prefix() == std::dynamic_pointer_cast<ModAssetArchive>(b)->Prefix(),
            "caminho e prefixo precisam permanecer iguais");
    Require(reopened->HasFile("objects/shared") && Bytes(reopened->LoadFile("objects/shared")) == "B" &&
                reopened->HasFile("mod/a/private/new"),
            "aliases ou arquivo exclusivamente privado ausentes");
    Require(failed, "alteracao de nomes na reabertura deveria abortar");
    const auto after = f.manager->GetArchiveSnapshot();
    Require(after.revision == current.revision && after.archives == current.archives &&
                *f.manager->ListFiles() == beforeFiles && f.rm.GetCachedResource("objects/shared", true) == cached,
            "recusa de nomes originais alterou VFS ou cache");
    Require(f.manager->RemoveArchive(b) == 1 && f.manager->HasFile("mod/a/objects/shared"),
            "remocao por identidade perdeu outro objeto de mesmo caminho/prefixo");
    std::cout << "PASS: conflito CAS, candidato invalido, arquivo alterado, remocao por identidade\n";
}
void LateFactory(bool negative) {
    Fixture f;
    auto a = f.Add("a", { { "objects/shared", "A" } });
    f.Add("b", { { "objects/shared", "B" } });
    auto factory = Register(f.rm);
    factory->returnNull = negative;
    std::promise<void> entered, release;
    auto ready = entered.get_future();
    auto barrier = release.get_future().share();
    factory->beforeReturn = [&] {
        entered.set_value();
        barrier.wait();
    };
    auto worker = std::async(std::launch::async, [&] {
        return f.rm.LoadResourceProcess(Ship::ResourceIdentifier("objects/shared", 42, a), true, InitData());
    });
    // Libera também no caminho de exceção, antes do destrutor do future esperar pelo worker.
    struct Release {
        std::promise<void>& promise;
        ~Release() {
            try {
                promise.set_value();
            } catch (...) {}
        }
    } guard{ release };
    Require(ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "fabrica nao iniciou");
    auto expected = f.manager->GetArchiveSnapshot();
    auto plan = f.Plan(expected, { a });
    Archives published;
    Require(f.rm.PublishArchiveBatch(expected, plan.desired, plan.affected, published),
            "publicacao com fabrica pendente");
    release.set_value();
    auto old = worker.get();
    Require(negative ? !old : std::dynamic_pointer_cast<TextResource>(old)->text == "A", "leitura antiga foi revogada");
    const auto line = f.rm.CheckCache(Ship::ResourceIdentifier("objects/shared", 42, a), true);
    Require(line.index() == 0 && std::get<0>(line) == std::remove_cvref_t<decltype(std::get<0>(line))>::NotCached,
            "V3: fabrica antiga repovoou cache positivo ou NotFound");
    factory->beforeReturn = {};
    factory->returnNull = false;
    const auto fresh = f.rm.LoadResourceProcess(Ship::ResourceIdentifier("objects/shared", 42, a), true, InitData());
    Require(std::dynamic_pointer_cast<TextResource>(fresh)->text == "B", "nova consulta nao resolveu B");
    std::cout << "PASS: epoch impede gravacao tardia " << (negative ? "NotFound" : "positiva") << " em owner/parent\n";
}
void ConcurrentRawRead() {
    Fixture f;
    auto a = f.Add("a", { { "objects/shared", "A" } });
    auto b = f.Add("b", { { "objects/shared", "B" } });
    auto backend = f.backends.at(a.get());
    std::promise<void> entered, release;
    auto ready = entered.get_future();
    auto barrier = release.get_future().share();
    backend->onRead = [&] {
        entered.set_value();
        barrier.wait();
    };
    auto reader = std::async(std::launch::async, [&] { return f.manager->LoadFile("objects/shared"); });
    struct Release {
        std::promise<void>& promise;
        ~Release() {
            try {
                promise.set_value();
            } catch (...) {}
        }
    } guard{ release };
    Require(ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "leitor nao iniciou");
    auto expected = f.manager->GetArchiveSnapshot();
    auto plan = f.Plan(expected, { a });
    Archives published;
    Require(f.rm.PublishArchiveBatch(expected, plan.desired, plan.affected, published),
            "publicacao com leitor pendente");
    Require(backend->closes == 0, "Close durante leitura anterior");
    Require(Bytes(f.manager->LoadFile("objects/shared")) == "B", "nova leitura durante leitura antiga nao resolveu B");
    release.set_value();
    Require(Bytes(reader.get()) == "A", "leitura antiga nao terminou com A");
    backend->onRead = {};
    expected.archives.clear();
    f.privateArchives.clear();
    a.reset();
    Require(backend->closes == 1, "aposentadoria nao fechou backend ao sair ultimo snapshot");
    std::cout << "PASS V2: leitor em andamento retido, novo leitor B, Close apos ultimo snapshot\n";
}

void SidecarCache() {
    Fixture f;
    auto a = f.Add("a", { { "objects/alias.meta", "A" } });
    auto b = f.Add("b", { { "objects/alias.meta", "B" } });
    auto factory = Register(f.rm);
    f.rm.LoadResourceProcess(Ship::ResourceIdentifier("objects/alias.meta", 77, b), true, InitData());
    f.rm.CacheExternalResource("objects/alias", std::make_shared<TextResource>("A"));
    f.rm.CacheExternalResource("alt/objects/alias", std::make_shared<TextResource>("alt A"));
    f.rm.CacheExternalResource("unrelated", std::make_shared<TextResource>("keep"));
    auto expected = f.manager->GetArchiveSnapshot();
    auto plan = f.Plan(expected, { a });
    Archives published;
    Require(f.rm.PublishArchiveBatch(expected, plan.desired, plan.affected, published), "publicacao meta");
    Require(!f.rm.GetCachedResource("objects/alias", true) && !f.rm.GetCachedResource("alt/objects/alias", true),
            "cache de contraparte meta/alt permaneceu");
    Require(!f.rm.GetCachedResource(Ship::ResourceIdentifier("objects/alias.meta", 77, b), true),
            "outro owner nao invalidado");
    Require(f.rm.GetCachedResource("unrelated", true) != nullptr, "cache nao afetado foi perdido");
    std::cout << "PASS: invalidacao meta, alt, todos owners/parents; preserva nome nao afetado\n";
}

void NegativeDirtyAltCache() {
    Fixture f;
    f.Add("base", { { "objects/shared", "base bytes" } });
    auto factory = Register(f.rm);
    int calls = 0;
    factory->beforeReturn = [&] { ++calls; };
    auto cached = std::make_shared<TextResource>("base ja inicializada");
    f.rm.CacheExternalResource("objects/shared", cached);
    auto dirty = std::make_shared<TextResource>("alternativo antigo");
    dirty->Dirty();
    f.rm.CacheExternalResource("alt/objects/shared", dirty);
    f.rm.SetAltAssetsEnabled(true);
    // Sem rejeitados, publicação ou mudança de epoch. O alternativo não existe, nem como sidecar.
    for (int i = 0; i < 3; ++i)
        Require(f.rm.LoadResourceProcess("objects/shared", false, InitData()) == cached,
                "base limpa deveria voltar do cache");
    std::cout << "Fabrica da base: " << calls << " chamadas em 3 consultas\n";
    Require(calls == 0, "alternativo dirty ilegivel repetiu a fabrica da base limpa");
    const auto line = f.rm.CheckCache("alt/objects/shared", true);
    Require(line.index() == 0 && std::get<0>(line) == std::remove_cvref_t<decltype(std::get<0>(line))>::NotFound,
            "alternativo dirty ilegivel deveria virar NotFound");
    std::cout << "PASS: alternativo dirty ilegivel vira NotFound; base limpa preservada sem repetir fabrica\n";
}

// Caminho de reserva do bootstrap: a redistribuição falhou e os rejeitados saem pelo mesmo lote. A retirada um a um
// (RetirePrivateModArchive) deixava no cache a leitura de A feita por um owner explícito.
void RetirementOnly() {
    Fixture f;
    auto a = f.Add("a", { { "objects/shared", "A" } });
    auto b = f.Add("b", { { "objects/shared", "B" }, { "objects/own", "B own" } });
    auto factory = Register(f.rm);
    Require(Bytes(f.manager->LoadFile("objects/shared")) == "A", "A deveria ter o alias antes da retirada");
    f.rm.LoadResourceProcess(Ship::ResourceIdentifier("objects/shared", 77, a), true, InitData());
    Require(f.rm.GetCachedResource(Ship::ResourceIdentifier("objects/shared", 77, a), true) != nullptr,
            "leitura de A por owner explicito nao entrou no cache");
    auto expected = f.manager->GetArchiveSnapshot();
    auto plan = PlanRetirement(expected.archives, f.privateArchives, { a });
    Require(plan.replacements == 0 && plan.desired.size() + 1 == expected.archives.size(),
            "retirada nao deveria reabrir wrappers");
    Archives published;
    Require(f.rm.PublishArchiveBatch(expected, plan.desired, plan.affected, published), "publicacao da retirada");
    Require(!f.manager->HasFile("objects/shared"), "retirada redistribuiu o alias de A");
    Require(!f.rm.GetCachedResource(Ship::ResourceIdentifier("objects/shared", 77, a), true),
            "cache de A com owner explicito permaneceu");
    Require(Bytes(f.manager->LoadFile("objects/own")) == "B own" &&
                Bytes(f.manager->LoadFile("mod/b/objects/shared")) == "B",
            "B perdeu alias ou nome privado");
    std::cout << "PASS: retirada sem redistribuicao invalida o cache de todos os owners\n";
}
} // namespace
int main() {
    try {
        ResidualAlias();
        NormalAndFirstWinner();
        ConflictsAndFailure();
        LateFactory(false);
        LateFactory(true);
        ConcurrentRawRead();
        SidecarCache();
        NegativeDirtyAltCache();
        RetirementOnly();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
