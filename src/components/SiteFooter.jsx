import './SiteFooter.css';

export default function SiteFooter() {
  return (
    <footer className="foot-stmt">
      <p className="foot-stmt__line">Приватные внешние модификации для GTA V.</p>
      <div className="foot-stmt__meta">
        <span className="foot-stmt__brand">shadow</span>
        <nav className="foot-stmt__links" aria-label="Юридическая информация">
          <a className="foot-stmt__link" href="#">Пользовательское соглашение</a>
          <a className="foot-stmt__link" href="#">Политика конфиденциальности</a>
        </nav>
        <span className="foot-stmt__copy">© 2026 shadow</span>
      </div>
      <div className="foot-stmt__social">
        <a className="foot-stmt__social-link" href="#" aria-label="Discord">Discord</a>
        <a className="foot-stmt__social-link" href="#" aria-label="Telegram">Telegram</a>
        <a className="foot-stmt__social-link" href="#" aria-label="YouTube">YouTube</a>
      </div>
    </footer>
  );
}
