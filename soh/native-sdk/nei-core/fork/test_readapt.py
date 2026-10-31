"""Testes de integração: Git lê repositórios temporários criados por Python.

NEI_FORK_DIR pode apontar para os módulos originais do fork. Nenhum teste escreve
nesse diretório; overlay.py e extract_additions.py são copiados para cada fixture.
"""
import hashlib
import importlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zlib

import readapt


HERE = Path(__file__).resolve().parent
FONTE = "soh/src/code/exemplo.c"
FUNCAO = "Exemplo"


def modulos_fork():
    candidatos = ([Path(os.environ["NEI_FORK_DIR"])] if "NEI_FORK_DIR" in os.environ else [])
    candidatos.append(HERE)
    for pasta in candidatos:
        if all((pasta / nome).is_file() for nome in ("overlay.py", "extract_additions.py")):
            return pasta
    raise RuntimeError("informe NEI_FORK_DIR com overlay.py e extract_additions.py para os testes")


def corpo(a=1, b=8):
    linhas = ["int Exemplo(void) {"]
    linhas += ["    int valor%d = %d;" % (i, a if i == 1 else b if i == 8 else i) for i in range(1, 11)]
    return "\n".join(linhas + ["    return valor1 + valor8;", "}", ""])


class RepoTemporario:
    """Escreve o formato Git mínimo; não executa init, add, commit ou checkout."""

    def __init__(self, pasta):
        self.pasta = pasta
        self.git = pasta / ".git"
        (self.git / "objects").mkdir(parents=True)
        (self.git / "refs/heads").mkdir(parents=True)
        (self.git / "HEAD").write_text("ref: refs/heads/teste\n", encoding="ascii")
        (self.git / "config").write_text("[core]\n\trepositoryformatversion = 0\n\tbare = false\n", encoding="ascii")
        self.relogio = 1700000000

    def objeto(self, tipo, dados):
        bruto = tipo.encode("ascii") + b" " + str(len(dados)).encode("ascii") + b"\0" + dados
        sha = hashlib.sha1(bruto).hexdigest()
        caminho = self.git / "objects" / sha[:2] / sha[2:]
        caminho.parent.mkdir(exist_ok=True)
        caminho.write_bytes(zlib.compress(bruto))
        return sha

    def arvore(self, arquivos):
        diretorio = {}
        for caminho, texto in arquivos.items():
            atual = diretorio
            partes = caminho.split("/")
            for parte in partes[:-1]:
                atual = atual.setdefault(parte, {})
            atual[partes[-1]] = texto.encode("utf-8")

        def gravar_arvore(no):
            entradas = []
            for nome, valor in sorted(no.items(), key=lambda par: par[0] + ("/" if isinstance(par[1], dict) else "")):
                modo, sha = ("40000", gravar_arvore(valor)) if isinstance(valor, dict) else (
                    "100644", self.objeto("blob", valor))
                entradas.append((modo + " " + nome).encode("utf-8") + b"\0" + bytes.fromhex(sha))
            return self.objeto("tree", b"".join(entradas))
        return gravar_arvore(diretorio)

    def commit(self, texto, pai=None, cabeca=True):
        self.relogio += 1
        linhas = ["tree " + self.arvore({FONTE: texto})]
        if pai:
            linhas.append("parent " + pai)
        identidade = "Fixture <fixture@example.invalid> %d +0000" % self.relogio
        linhas += ["author " + identidade, "committer " + identidade, "", "fixture", ""]
        sha = self.objeto("commit", "\n".join(linhas).encode("utf-8"))
        if cabeca:
            (self.git / "refs/heads/teste").write_text(sha + "\n", encoding="ascii")
            destino = self.pasta / FONTE
            destino.parent.mkdir(parents=True, exist_ok=True)
            destino.write_text(texto, encoding="utf-8", newline="\n")
        return sha


class ReadaptTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.modulos = modulos_fork()
        sys.dont_write_bytecode = True
        sys.path.insert(0, str(cls.modulos))
        cls.overlay = importlib.import_module("overlay")

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="teste-readapt-")
        self.addCleanup(self.temp.cleanup)
        self.raiz = Path(self.temp.name)
        self.repo = RepoTemporario(self.raiz / "host")
        self.fork_dir = self.repo.pasta / readapt.PASTA_FORK
        self.fork_dir.mkdir(parents=True)
        for nome in ("overlay.py", "extract_additions.py"):
            shutil.copyfile(self.modulos / nome, self.fork_dir / nome)
        (self.repo.pasta / "soh/hookable_sources.txt").write_text("src/code/exemplo.c\n", encoding="utf-8")
        self.base = self.repo.commit(corpo())
        self.fork = self.repo.commit(corpo(b=80), self.base, cabeca=False)
        self.velha = self.repo.commit(corpo(a=2), self.base)
        self.atual = self.repo.commit(corpo(a=3), self.velha)
        self.sync(self.base, self.fork)

    def sync(self, base, fork):
        (self.fork_dir / "sync.py").write_text("FORK_BASE = %r\nFORK_COMMIT = %r\n" % (base, fork), encoding="utf-8")

    def assinatura(self, texto, tipo="extra"):
        # O parser do overlay define exatamente o trecho que participa do hash.
        texto = self.overlay.funcoes(texto)[FUNCAO].texto
        base, fork = (self.overlay.funcoes(t)[FUNCAO].texto for t in (corpo(), corpo(b=80))) if tipo == "merge" else ("", "")
        return self.overlay.assinatura(texto, base, fork)

    def resolucao(self, tipo="extra", atual=False, conflito=False, eol="\n", assinatura=None):
        pasta = self.fork_dir / ("overlay-extra" if tipo == "extra" else "overlay-merges")
        pasta.mkdir(exist_ok=True)
        caminho = pasta / "exemplo.Exemplo.c"
        host = corpo(a=3 if atual else 2)
        assinatura = assinatura or self.assinatura(host, tipo)
        adaptada = corpo(a=9 if conflito else 3 if atual else 2, b=80)
        conteudo = "/* overlay-%s %s */\n%s" % (tipo, assinatura, adaptada)
        caminho.write_bytes(conteudo.replace("\n", eol).encode("utf-8"))
        return caminho

    def executar(self, *opcoes):
        return subprocess.run([sys.executable, "-B", str(HERE / "readapt.py"), str(self.repo.pasta),
                               *(str(o) for o in opcoes)], capture_output=True, text=True, encoding="utf-8")

    def confirmar_limpa(self, tipo, eol):
        caminho = self.resolucao(tipo=tipo, eol=eol)
        r = self.executar(caminho)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn(self.velha, r.stdout)
        dados = caminho.read_bytes()
        self.assertIn(self.assinatura(corpo(a=3), tipo).encode(), dados)
        self.assertIn(b"valor1 = 3;", dados)
        self.assertIn(b"valor8 = 80;", dados)
        self.assertNotIn(b"<<<<<<<", dados)
        if eol == "\r\n":
            self.assertNotIn(b"\n", dados.replace(b"\r\n", b""))
        else:
            self.assertNotIn(b"\r", dados)

    def test_carimbo_em_dia_nada_muda(self):
        for tipo in ("extra", "merge"):
            with self.subTest(tipo=tipo):
                caminho = self.resolucao(tipo, atual=True)
                antes = caminho.read_bytes(), caminho.stat().st_mtime_ns
                r = self.executar(caminho)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertEqual(antes, (caminho.read_bytes(), caminho.stat().st_mtime_ns))
                self.assertIn("em dia", r.stdout)

    def test_fonte_excluida_ignorada(self):
        (self.fork_dir / "overlay-exclude.txt").write_text("arquivo exemplo.c  # fora do overlay\n",
                                                           encoding="utf-8")
        for tipo in ("extra", "merge"):
            with self.subTest(tipo=tipo):
                caminho = self.resolucao(tipo)
                antes = caminho.read_bytes()
                for opcoes in (("--verificar",), ()):
                    r = self.executar(*opcoes)
                    self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                    self.assertIn("ignorada", r.stdout)
                self.assertEqual(antes, caminho.read_bytes())
                caminho.unlink()

    def test_extra_mescla_limpa_preserva_eol(self):
        for eol in ("\n", "\r\n"):
            with self.subTest(eol=repr(eol)):
                self.confirmar_limpa("extra", eol)

    def test_merge_mescla_limpa_preserva_eol(self):
        for eol in ("\n", "\r\n"):
            with self.subTest(eol=repr(eol)):
                self.confirmar_limpa("merge", eol)

    def test_conflito_preserva_original(self):
        caminho = self.resolucao(conflito=True, eol="\r\n")
        antes = caminho.read_bytes()
        r = self.executar(caminho)
        self.assertEqual(r.returncode, 1, r.stderr)
        self.assertEqual(caminho.read_bytes(), antes)
        dados = caminho.with_suffix(".conflito.c").read_bytes()
        self.assertIn(b"<<<<<<< host atual", dados)
        self.assertIn(b">>>>>>> resolu", dados)
        self.assertNotIn(b"\n", dados.replace(b"\r\n", b""))

    def test_saida_limpa_nao_altera_origem(self):
        caminho = self.resolucao()
        antes = caminho.read_bytes()
        saida = self.raiz / "saida"
        r = self.executar(caminho, "--saida", saida)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(caminho.read_bytes(), antes)
        self.assertIn(b"valor1 = 3;", (saida / caminho.name).read_bytes())

    def test_saida_conflito_nao_grava_resolucao_normal(self):
        caminho = self.resolucao("merge", conflito=True)
        antes = caminho.read_bytes()
        saida = self.raiz / "saida"
        r = self.executar(caminho, "--saida", saida)
        self.assertEqual(r.returncode, 1, r.stderr)
        self.assertEqual(caminho.read_bytes(), antes)
        self.assertFalse((saida / caminho.name).exists())
        self.assertTrue((saida / caminho.with_suffix(".conflito.c").name).is_file())

    def test_carimbo_sem_revisao(self):
        caminho = self.resolucao(assinatura="0123456789ab da39a3ee5e6b da39a3ee5e6b")
        antes = caminho.read_bytes()
        r = self.executar(caminho)
        self.assertEqual(r.returncode, 2)
        self.assertIn("carimbo não encontrado", r.stderr)
        self.assertIn("limite 200", r.stderr)
        self.assertEqual(caminho.read_bytes(), antes)

    def test_limite_configuravel(self):
        caminho = self.resolucao()
        r = self.executar(caminho, "--limite", "1")
        self.assertEqual(r.returncode, 2)
        self.assertIn("limite 1", r.stderr)
        r = self.executar(caminho, "--limite", "2")
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_verificar_velho_sem_gravar_ou_procurar_historico(self):
        caminho = self.resolucao()
        antes = caminho.read_bytes()
        # Histórico indisponível: verificar ainda deve funcionar.
        (self.repo.git / "HEAD").unlink()
        saida = self.raiz / "nao-criar"
        r = self.executar("--verificar", "--saida", saida)
        self.assertEqual(r.returncode, 1, r.stderr)
        self.assertIn("velho", r.stdout)
        self.assertEqual(caminho.read_bytes(), antes)
        self.assertFalse(saida.exists())

    def test_verificar_em_dia(self):
        caminho = self.resolucao(atual=True)
        r = self.executar("--verificar")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("Nenhum carimbo velho", r.stdout)
        r = self.executar(caminho, "--verificar")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout, "")

    def test_base_ou_fork_alterado_bloqueia(self):
        caminho = self.resolucao("merge")
        antes = caminho.read_bytes()
        for tipo in ("base", "fork"):
            with self.subTest(tipo=tipo):
                alterado = self.repo.commit(corpo(a=50), self.base, cabeca=False)
                self.sync(alterado if tipo == "base" else self.base, alterado if tipo == "fork" else self.fork)
                r = self.executar(caminho)
                self.assertEqual(r.returncode, 2)
                self.assertIn(tipo + " também mudou", r.stderr)
                self.assertEqual(caminho.read_bytes(), antes)
                self.assertFalse(caminho.with_suffix(".conflito.c").exists())
                v = self.executar(caminho, "--verificar")
                self.assertEqual(v.returncode, 1, v.stderr)

    def test_varredura_processa_so_velhos_ignora_artefatos(self):
        em_dia = self.resolucao("extra", atual=True)
        velho = self.resolucao("merge")
        antes = em_dia.read_bytes(), em_dia.stat().st_mtime_ns
        velho.with_suffix(".conflito.c").write_text("marcadores antigos", encoding="utf-8")
        r = self.executar()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertNotIn("em dia", r.stdout)
        self.assertEqual(antes, (em_dia.read_bytes(), em_dia.stat().st_mtime_ns))
        self.assertIn(self.assinatura(corpo(a=3), "merge"), velho.read_text(encoding="utf-8"))

    def test_carimbo_invalido_e_conflito_preexistente(self):
        caminho = self.resolucao()
        for dados in (b"sem carimbo\nint Exemplo(void) {}\n", caminho.read_bytes() + b"<<<<<<< antigo\n"):
            with self.subTest(dados=dados[:20]):
                caminho.write_bytes(dados)
                r = self.executar(caminho)
                self.assertEqual(r.returncode, 2)
                self.assertEqual(caminho.read_bytes(), dados)

    def test_saida_igual_origem_recusada(self):
        caminho = self.resolucao()
        antes = caminho.read_bytes()
        r = self.executar(caminho, "--saida", caminho.parent)
        self.assertEqual(r.returncode, 2)
        self.assertIn("escolha outra pasta", r.stderr)
        self.assertEqual(caminho.read_bytes(), antes)

    def test_bom_preservado(self):
        caminho = self.resolucao()
        caminho.write_bytes(b"\xef\xbb\xbf" + caminho.read_bytes())
        r = self.executar(caminho)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(caminho.read_bytes().startswith(b"\xef\xbb\xbf/* overlay-extra"))

    def test_fonte_ambigua_recusada(self):
        caminho = self.resolucao()
        fontes = self.repo.pasta / "soh/hookable_sources.txt"
        fontes.write_text("src/code/exemplo.c\nsrc/outro/exemplo.c\n", encoding="utf-8")
        r = self.executar(caminho)
        self.assertEqual(r.returncode, 2)
        self.assertIn("encontrados 2", r.stderr)

    def test_nomes_duplicados_na_saida_recusados(self):
        a = self.resolucao("extra")
        b = self.resolucao("merge")
        r = self.executar(a, b, "--saida", self.raiz / "saida")
        self.assertEqual(r.returncode, 2)
        self.assertIn("nomes de saída duplicados", r.stderr)

    def test_funcao_ausente_no_host(self):
        caminho = self.resolucao()
        (self.repo.pasta / FONTE).write_text("int Outra(void) { return 0; }\n", encoding="utf-8")
        r = self.executar(caminho)
        self.assertEqual(r.returncode, 2)
        self.assertIn("função Exemplo ausente", r.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
